#include "plugin/plugin.hpp"
#include "agent/agent.hpp"
#include "cmd/cmd.hpp"

extern "C" const msa::plugin::Info *msa_plugin_register(const msa::PluginHooks *hooks);

namespace dekarrin {

	typedef struct env
	{
		std::vector<msa::cmd::Command> *commands;
	} Env;

	static int init(msa::Handle hdl, void **env);
	static int quit(msa::Handle hdl, void *env);
	static int add_commands(msa::Handle hdl, void *plugin_env, std::vector<msa::cmd::Command *> &new_commands);
	static msa::cmd::Result love_func(msa::Handle hdl, const msa::cmd::ParamList &params, msa::event::HandlerSync *const sync);
	
	static const msa::plugin::FunctionTable function_table = {init, quit, NULL, NULL, NULL, add_commands};
	static const msa::plugin::Info plugin_info = {"example", "Example Plugin", {"dekarrin"}, msa::plugin::Version(1, 0, 0, 0), &function_table};

	static const msa::PluginHooks *msa_sys;

	static int init(msa::Handle hdl __attribute__((unused)), void **env)
	{
		Env *my_env = new Env;
		my_env->commands = new std::vector<msa::cmd::Command>;
		my_env->commands->push_back(msa::cmd::Command("LOVE", "execute a test function", "", love_func));
		*env = my_env;
		return 0;
	}

	static int quit(msa::Handle hdl __attribute__((unused)), void *env)
	{
		Env *my_env = (Env *) env;
		delete my_env->commands;
		delete my_env;
		return 0;
	}

	static int add_commands(msa::Handle hdl __attribute__((unused)), void *env, std::vector<msa::cmd::Command *> &new_commands)
	{
		Env *my_env = (Env *) env;
		for (size_t i = 0; i < my_env->commands->size(); i++)
		{
			new_commands.push_back(&my_env->commands->at(i));
		}
		return 0;
	}
	
	static msa::cmd::Result love_func(msa::Handle hdl, const msa::cmd::ParamList &args __attribute__((unused)), msa::event::HandlerSync *const sync __attribute__((unused)))
	{
		msa_sys->agent->say(hdl, "$USER_TITLE, the new command works!");
		return msa::cmd::Result(0);
	}

}

extern "C" const msa::plugin::Info *msa_plugin_register(const msa::PluginHooks *hooks)
{
	dekarrin::msa_sys = hooks;
	return &dekarrin::plugin_info;
}

