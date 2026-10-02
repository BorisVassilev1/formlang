#pragma once

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <istream>
#include <unistd.h>

#include <poll.h>
#include <sys/wait.h>

constexpr unsigned int BUFFER_SIZE = 4096;

inline void waitREAD(int pipe, int timeout = -1);

inline void waitWRITE(int pipe, int timeout = -1);

class PipeBuffer : public std::streambuf {
   public:
	explicit PipeBuffer(int pipe_fd);

	PipeBuffer(const PipeBuffer &)			  = delete;
	PipeBuffer &operator=(const PipeBuffer &) = delete;
	~PipeBuffer() override;
	void close();
	void setPipe(int pipe);

   protected:
	int_type underflow() override;
	int_type overflow(int_type c = traits_type::eof()) override;
	int		 sync() override;

   private:
	int	 pipe_fd;
	char buffer[BUFFER_SIZE];
	char output_buffer[BUFFER_SIZE];
};

class Pipe {
   public:
	Pipe();
	Pipe(const Pipe &)			  = delete;
	Pipe &operator=(const Pipe &) = delete;
	Pipe(Pipe &&s);
	Pipe &operator=(Pipe &&s);

	Pipe(int pipe);
	~Pipe();

	operator int() const;

	void waitREAD(int timeout = -1) const;
	void waitWRITE(int timeout = -1) const;

   private:
	int pipe = 0;
};

class PipeStream : public std::iostream {
   public:
	PipeStream();
	PipeStream(const Pipe &s);

	PipeStream(int pipe);

	~PipeStream() override;
	PipeStream &operator=(int pipe);

	const Pipe &getpipe();
	void		close();

   private:
	PipeBuffer	buffer;		// Our custom stream buffer
	const Pipe *pipe;
};

class Process {
   public:
	Process(const Process &)			= delete;
	Process &operator=(const Process &) = delete;
	Process(Process &&s);
	Process &operator=(Process &&s);

	template <typename... Args>
	Process(const char *path, const Args... args) : pid(-1) {
		int in_pipe[2], out_pipe[2], err_pipe[2];
		if (pipe(in_pipe) == -1) { throw std::runtime_error("pipe failed"); }
		if (pipe(out_pipe) == -1) { throw std::runtime_error("pipe failed"); }
		if (pipe(err_pipe) == -1) { throw std::runtime_error("pipe failed"); }

		pid = fork();
		if (pid == -1) { throw std::runtime_error("fork failed"); }

		if (pid == 0) {
			close(in_pipe[1]);
			close(out_pipe[0]);
			close(err_pipe[0]);

			dup2(in_pipe[0], STDIN_FILENO);
			dup2(out_pipe[1], STDOUT_FILENO);
			dup2(err_pipe[1], STDERR_FILENO);

			close(in_pipe[0]);
			close(out_pipe[1]);
			close(err_pipe[1]);

			execlp(path, path, args..., nullptr);
			exit(1);
		}

		close(in_pipe[0]);
		close(out_pipe[1]);
		close(err_pipe[1]);

		in_stream  = (in_pipe[1]);
		out_stream = (out_pipe[0]);
		err_stream = (err_pipe[0]);
	}
	~Process();

	int wait();

	PipeStream &in();
	PipeStream &out();
	PipeStream &err();

   private:
	pid_t	   pid;
	PipeStream in_stream, out_stream, err_stream;
};

class ShellProcess : public Process {
   public:
	ShellProcess(const char *cmd);
};
