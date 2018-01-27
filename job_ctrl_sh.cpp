#include "job_ctrl_sh.h"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <regex.h>

using namespace std;
extern char **environ;

jcsh::jcsh(){
	// pid, pgid, shell's own group
	pid = getpid();
	pgid = getpid();
	if(setpgid(pid, pgid) < 0){
		perror("shell init error");
		exit(1);
	}
	// signal handler
	signal(SIGINT, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	// catch terminal
	tcsetpgrp(STDIN_FILENO, pgid);
}

void jcsh::run(){
	cout << "$ ";
	while(getline(cin, cmd)){
		if(cmd == "exit")break;
		else if(cmd == "fg"){
			_make_foreground();
		}
		else if(cmd == "bg"){
			_ls_background();
			_clear_terminated();
		}
		else if(cmd.substr(0, 6) == "export"){
			size_t found = cmd.find_first_of('=', 6);
			cmd[found] = ' ';
			stringstream ss;
			ss.str(cmd.substr(6));
			string name, value;
			ss >> name >> value;
			if(setenv(name.c_str(), value.c_str(), 1) < 0)
				perror("setenv");
		}
		else if(cmd.substr(0, 5) == "unset"){
			stringstream ss;
			ss.str(cmd.substr(5));
			string name;
			ss >> name;
			if(unsetenv(name.c_str()) < 0)
				perror("unset");
		}
		else if(cmd.empty()){
			;
		}
		else{
			_launch_job();
		}
		tcsetpgrp(STDIN_FILENO, pgid);
		cout << "$ ";
	}
}

void jcsh::_launch_job(){
	// foreground or background job
	size_t found = cmd.find(" &");
	bool foreground_job;
	if(found == string::npos)
		foreground_job = true;	
	else
		foreground_job = false;
	job_vec.push_back(job(cmd, foreground_job));
	// fork to handle this job
	pid_t childpid;
	if((childpid = fork()) < 0){
		perror("job fork");
		exit(1);
	}
	else if(!childpid){ 
		job_vec.back().run();
		exit(0);
	}
	else{
		job_vec.back().pid = childpid;
		job_vec.back().pgid = childpid;
		if(foreground_job){
			int status;
			waitpid(childpid, &status, WUNTRACED);
			tcsetpgrp(STDIN_FILENO, pgid);
		}
	}
}

void jcsh::_make_foreground(){
	if(!job_vec.empty()){
		cout << job_vec.back().cmd << endl;
		tcsetpgrp(STDIN_FILENO, job_vec.back().pgid);
		kill(-job_vec.back().pid, SIGCONT);
		int status;
		waitpid(job_vec.back().pid, &status, WUNTRACED);
	}
}

void jcsh::_ls_background(){
	for(int i=0; i<job_vec.size(); i++){
		cout << "[" << i+1 << "]" ;
		int status;
		waitpid(job_vec[i].pid, &status, WUNTRACED | WNOHANG);
		if(WIFEXITED(status))
			cout << " Done       : " ;
		else if(WIFSTOPPED(status))
			cout << " Stopped    : " ;
		else if(WIFSIGNALED(status)) // ctrl + z
			cout << " Terminated : ";
		else if(WIFCONTINUED(status))
			cout << " Resumed    : ";
		else
			cout << " Others     : ";
		cout << job_vec[i].cmd << endl;
	}
}

void jcsh::_clear_terminated(){
	for(int i=0; i<job_vec.size(); i++){
		int status;
		waitpid(job_vec[i].pid, &status, WUNTRACED | WNOHANG);
		if(WIFEXITED(status))
			job_vec.erase(job_vec.begin()+i--);
	}
}

job::job(string s, bool fg):cmd(s), catch_terminal(fg){
	;
}

void job::run(){
	// set own group after fork
	pid = getpid();
	pgid = getpid();
	if(setpgid(pid, pgid) < 0){
		perror("job setpgid error");
		exit(1);
	}
	// handle signal
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	// parser
	_parse_cmd();
	// pipe, redir_in, redir_out
	int pip[2], in=STDIN_FILENO, out=STDOUT_FILENO;
	if(redir_in.length()){
		if((in = open(redir_in.c_str(), O_RDONLY)) < 0){
			perror("open file for redirect in error");
			exit(1);
		}
	}
	for(int i=0; i<proc_vec.size(); i++){
		// if this is the last one or not
		if((i+1) != proc_vec.size()){
			if(pipe(pip) < 0){
				perror("pipe error");
				exit(1);
			}
			out = pip[1];
		}
		else{
			if(redir_out.length()){
				if((out = open(redir_out.c_str(), O_WRONLY | O_CREAT, 
					S_IRWXU | S_IRWXG | S_IRWXO)) < 0){
					perror("open file for redirect out error");
					exit(1);
				}
			}
			else{
				out = STDOUT_FILENO;
			}
		}
		// fork and exec proc
		pid_t childpid;
		if((childpid = fork()) < 0){
			perror("fork error");
			exit(1);
		}
		else if(!childpid){ 
			proc_vec[i].run(in, out);
		}
		else { 
			setpgid(childpid, pgid);
			proc_vec[i].pid = childpid;
			int status;
			waitpid(childpid, &status, WUNTRACED);
			if(in != STDIN_FILENO)
				close(in);
			if(out != STDOUT_FILENO)
				close(out);
			in = pip[0];
		}
	}
	// after fork all sub proc, parent should wait them be finished.
	// while(!_all_completed());
}

void job::_parse_cmd(){
	// if foreground, tcsetpgrp
	if(catch_terminal){
		if(tcsetpgrp(STDIN_FILENO, pgid) < 0){
			perror("job catch foreground error");
			exit(1);
		}
	}
	stringstream ss;
	ss.str(cmd);
	string proc_cmd;
	string token;
	while(ss >> token){
		if(token == "|"){
			proc_vec.push_back(proc(proc_cmd, catch_terminal));
			proc_cmd.clear();
		}
		else if(token == "<"){
			ss >> redir_in;
		}
		else if(token == ">"){
			ss >> redir_out;
		}
		else if(token == "&"){
			catch_terminal = false;
		}
		else{
			proc_cmd += " " + token;
		}
	}
	proc_vec.push_back(proc(proc_cmd, catch_terminal));
}

bool job::_all_completed(){
	_mark_all_status();
	for(int i=0; i<proc_vec.size(); i++)
		if(!proc_vec[i].completed)
			return false;
	return true;
}

void job::_mark_all_status(){
	for(int i=0; i<proc_vec.size(); i++){
		int status;
		waitpid(proc_vec[i].pid, &status, WUNTRACED | WNOHANG);
		if(WIFEXITED(status))
			proc_vec[i].completed = true;
	}
}

proc::proc(string s, bool fg):
cmd(s), completed(false), catch_terminal(fg){
	;
}

void proc::run(int in, int out){
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	if(in != STDIN_FILENO){
		dup2(in, STDIN_FILENO);
		close(in);
	}
	if(out != STDOUT_FILENO){
		dup2(out, STDOUT_FILENO);
		close(out);
	}
	stringstream ss;
	ss.str(cmd);
	string token;
	vector<string> token_vec;
	while(ss >> token){
		// handle *?
		size_t found = token.find_first_of("*?");
		if(found != string::npos){
			for(int i=0; i<token.size(); i++){
				if(token[i] == '*')
					token.insert(i++, ".");
				else if(token[i] == '?')
					token[i] = '.';
				else
					continue;
			}
			DIR *dir;
			struct dirent *ent;
			bool matched = false;
			if((dir = opendir(".")) != NULL){
				regex_t r;
				regmatch_t p[1];
				regcomp(&r, token.c_str(), REG_NEWLINE);
				while((ent = readdir(dir)) != NULL){
					if(regexec(&r, ent->d_name, 1, p, REG_NOTEOL) == 0){
						if(p[0].rm_eo-p[0].rm_so == token.length()){
							matched = true;
							token_vec.push_back(string(ent->d_name));
						}
					}
				}
				regfree(&r);
				if(!matched)
					token_vec.push_back(token);
				closedir(dir);
			}
		}
		else
			token_vec.push_back(token);
	}
	char *argv[token_vec.size() + 1];
	for(int i=0; i<token_vec.size(); i++)
		argv[i] = const_cast<char*>(token_vec[i].c_str());
	argv[token_vec.size()] = NULL;
	execvp(argv[0], argv);
	perror("execvp error");
	exit(1);
}
