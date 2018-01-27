#ifndef JOB_CTRL_SH_H
#define JOB_CTRL_SH_H
#include <string>
#include <vector>

class proc{
public:
	proc(std::string, bool);
	void run(int, int);
	pid_t pid;
	bool completed;
private:
	bool catch_terminal;
	std::string cmd;
};

class job{
public:
	job(std::string, bool);
	void run();
	pid_t pid;
	pid_t pgid;
	std::string cmd;
private:
	bool catch_terminal;
	std::string redir_in;
	std::string redir_out;
	std::vector<proc> proc_vec;
	void _parse_cmd();
	bool _all_completed();
	void _mark_all_status();
};

class jcsh{
public:
	jcsh();
	void run();
	pid_t pid;
	pid_t pgid;
private:
	std::string cmd;
	std::vector<job> job_vec;
	void _launch_job();
	void _make_foreground();
	void _ls_background();
	void _clear_terminated();
};

#endif
