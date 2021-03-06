#include "msa.hpp"
#include "agent/agent.hpp"
#include "input/input.hpp"
#include "cmd/cmd.hpp"
#include "event/dispatch.hpp"
#include "cfg/cfg.hpp"
#include "log/log.hpp"
#include "output/output.hpp"
#include "util/string.hpp"
#include "plugin/plugin.hpp"

#include <string>

#include "platform/thread/thread.hpp"

namespace msa {

	static PluginHooks *PLUGIN_HOOKS = NULL;
	
	// end of static symbols

	typedef int (*ModFunc)(Handle);
	typedef int (*ModInitFunc)(Handle, const msa::cfg::Section&);

	static const msa::cfg::Section &get_module_section(msa::cfg::Config *conf, const std::string &name);
	static int init_module(Handle hdl, msa::cfg::Config *conf, ModInitFunc init_func, const std::string &name);
	static int setup_module(Handle hdl, ModFunc setup_func, const std::string &name);
	static int quit_module(Handle msa, void **mod, ModFunc quit_func, const std::string &log_name);
	static int teardown_module(Handle hdl, void **mod, ModFunc teardown_func, const std::string &name);
	static int stop(Handle hdl, int retcode);

	static const msa::cfg::Section blank_section("");
	
	extern void init()
	{
		PLUGIN_HOOKS = new PluginHooks;
		PLUGIN_HOOKS->event = msa::event::get_plugin_hooks();
		PLUGIN_HOOKS->input = msa::input::get_plugin_hooks();
		PLUGIN_HOOKS->output = msa::output::get_plugin_hooks();
		PLUGIN_HOOKS->agent = msa::agent::get_plugin_hooks();
		PLUGIN_HOOKS->cmd = msa::cmd::get_plugin_hooks();
		PLUGIN_HOOKS->log = msa::log::get_plugin_hooks();
		PLUGIN_HOOKS->plugin = msa::plugin::get_plugin_hooks();
		msa::thread::init();
	}
	
	extern void quit()
	{
		msa::thread::quit();
		delete PLUGIN_HOOKS;
	}

	extern const PluginHooks *get_plugin_hooks()
	{
		return PLUGIN_HOOKS;
	}

	extern int start(Handle *msa, const char *config_path)
	{
		// load config first
		msa::cfg::Config *conf = msa::cfg::load(config_path);
		if (conf == NULL)
		{
			return MSA_ERR_CONFIG;
		}

		environment_type *hdl = new environment_type;
		hdl->status = Status::CREATED;
		hdl->event = NULL;
		hdl->input = NULL;
		hdl->output = NULL;
		hdl->agent = NULL;
		hdl->cmd = NULL;
		hdl->log = NULL;
		hdl->plugin = NULL;

		// init system modules; changing order could break everything
		if (init_module(hdl, conf, msa::log::init, "Log") != 0) return MSA_ERR_LOG;
		if (init_module(hdl, conf, msa::output::init, "Output") != 0) return MSA_ERR_OUTPUT;
		if (init_module(hdl, conf, msa::event::init, "Event") != 0) return MSA_ERR_EVENT;
		if (init_module(hdl, conf, msa::input::init, "Input") != 0) return MSA_ERR_INPUT;
		if (init_module(hdl, conf, msa::agent::init, "Agent") != 0) return MSA_ERR_AGENT;
		if (init_module(hdl, conf, msa::cmd::init, "Command") != 0) return MSA_ERR_CMD;
		if (init_module(hdl, conf, msa::plugin::init, "Plugin") != 0) return MSA_ERR_PLUGIN;
		
		// system is inited, do setup now (order does not matter)
		if (setup_module(hdl, msa::plugin::setup, "Plugin") != 0) return MSA_ERR_PLUGIN;
		if (setup_module(hdl, msa::event::setup, "Event") != 0) return MSA_ERR_EVENT;

		*msa = hdl;
		delete conf;
		
		msa::log::info(hdl, "Finished initializing Moe Serifu Agent");
		return MSA_SUCCESS;
	}

	extern int stop(Handle msa)
	{
		return stop(msa, 0);
	}

	extern int dispose(Handle msa)
	{
		// make sure our modules have been properly quit before deleting the pointer
		if (msa->event != NULL)
		{
			return MSA_ERR_EVENT;
		}

		if (msa->input != NULL)
		{
			return MSA_ERR_INPUT;
		}

		if (msa->agent != NULL)
		{
			return MSA_ERR_AGENT;
		}

		if (msa->cmd != NULL)
		{
			return MSA_ERR_CMD;
		}

		if (msa->output != NULL)
		{
			return MSA_ERR_OUTPUT;
		}

		if (msa->log != NULL)
		{
			return MSA_ERR_LOG;
		}

		if (msa->plugin != NULL)
		{
			return MSA_ERR_PLUGIN;
		}
		
		delete msa;
		return MSA_SUCCESS;
	}

	static int stop(Handle msa, int retcode)
	{
		msa::log::info(msa, "Moe Serifu Agent is now shutting down...");
		
		// teardown; order does not matter here. Skip if non-normal shutdown
		if (retcode == 0)
		{
			if (teardown_module(msa, (void **) &msa->plugin, msa::plugin::teardown, "Plugin") != 0) return MSA_ERR_PLUGIN;
			if (teardown_module(msa, (void **) &msa->event, msa::event::teardown, "Event") != 0) return MSA_ERR_EVENT;
		}
		
		// modules are torn down, now quit them; order matters
		if (quit_module(msa, (void **) &msa->plugin, msa::plugin::quit, "Plugin") != 0) return MSA_ERR_PLUGIN;
		if (quit_module(msa, (void **) &msa->input, msa::input::quit, "Input") != 0) return MSA_ERR_INPUT;
		if (quit_module(msa, (void **) &msa->agent, msa::agent::quit, "Agent") != 0) return MSA_ERR_AGENT;
		if (quit_module(msa, (void **) &msa->cmd, msa::cmd::quit, "Command") != 0) return MSA_ERR_CMD;
		if (quit_module(msa, (void **) &msa->event, msa::event::quit, "Event") != 0) return MSA_ERR_EVENT;
		if (quit_module(msa, (void **) &msa->output, msa::output::quit, "Output") != 0) return MSA_ERR_OUTPUT;
		
		msa::log::info(msa, "Moe Serifu Agent primary modules shutdown cleanly");
		
		// shutdown log module last
		if (quit_module(msa, (void **) &msa->log, msa::log::quit, "") != 0) return MSA_ERR_LOG;
		
		msa->status = msa::Status::STOPPED;
		return MSA_SUCCESS;
	}

	static const msa::cfg::Section &get_module_section(msa::cfg::Config *conf, const std::string &name)
	{
		if (conf->find(name) != conf->end())
		{
			return (*conf)[name];
		}
		else
		{
			return blank_section;
		}
	}
	
	static int setup_module(Handle hdl, ModFunc setup_func, const std::string &name)
	{
		std::string lower_name = name;
		msa::string::to_lower(lower_name);
		
		int ret = setup_func(hdl);
		if (ret != 0)
		{
			msa::log::error(hdl, "Failed to setup " + lower_name + " module");
			msa::log::debug(hdl, name + " module's setup() returned " + std::to_string(ret));
			stop(hdl, ret);
			dispose(hdl);
			return ret;
		}
		msa::log::trace(hdl, "Setup " + lower_name + " module");
		return ret;
	}
	
	static int teardown_module(Handle hdl, void **mod, ModFunc teardown_func, const std::string &name)
	{
		std::string lower_name = name;
		msa::string::to_lower(lower_name);
		
		int ret = 0;
		if (*mod != NULL)
		{
			int ret = teardown_func(hdl);
			if (ret != 0)
			{
				msa::log::error(hdl, "Failed to tear down " + lower_name + " module");
				msa::log::debug(hdl, name + " module's teardown() returned " + std::to_string(ret));
				return ret;
			}
			msa::log::trace(hdl, "Tore down " + lower_name + " module");
		}
		else
		{
			msa::log::trace(hdl, name + " module not started, no need to teardown");
		}
		return ret;
	}
	
	static int init_module(Handle hdl, msa::cfg::Config *conf, ModInitFunc init_func, const std::string &name)
	{
		std::string lower_name = name;
		std::string upper_name = name;
		msa::string::to_lower(lower_name);
		msa::string::to_upper(upper_name);
		bool enable_failure_log = (upper_name != "LOG"); // cant log messages before log is started
		
		msa::cfg::Section section = get_module_section(conf, upper_name);
		int ret = init_func(hdl, section);
		if (ret != 0)
		{
			if (enable_failure_log)
			{
				msa::log::error(hdl, "Failed to start " + lower_name + " module");
				msa::log::debug(hdl, name + " module's init() returned " + std::to_string(ret));
			}
			stop(hdl, ret);
			dispose(hdl);
			return ret;
		}
		msa::log::trace(hdl, "Started " + lower_name + " module");
		return ret;
	}
	
	static int quit_module(Handle msa, void **mod, ModFunc quit_func, const std::string &log_name)
	{
		std::string lower_name = log_name;
		msa::string::to_lower(lower_name);
		int status = 0;
		if (*mod != NULL)
		{
			status = quit_func(msa);
			if (status != 0)
			{
				if (log_name != "")
				{
					msa::log::error(msa, "Failed to stop " + lower_name + " module");
					msa::log::debug(msa, log_name + " module's quit() returned " + std::to_string(status));
				}
				return status;
			}
			*mod = NULL;
			if (log_name != "")
			{
				msa::log::trace(msa, "Stopped " + lower_name + " module");
			}
		}
		else if (log_name != "")
		{
			msa::log::trace(msa, log_name + " module not started, no need to stop");
		}
		return status;
	}

}
