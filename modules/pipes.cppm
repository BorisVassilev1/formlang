module;

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <istream>
#include <unistd.h>

#include <poll.h>
#include <sys/wait.h>

#include "debug_macros.hpp"

export module formlang:pipes;

import :dbg;

#define BUFFER_SIZE 4096

export inline void waitREAD(int pipe, int timeout = -1) {
	struct pollfd pfd;
	pfd.fd		= pipe;
	pfd.events	= POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, timeout) == -1) { throw std::runtime_error("poll failed"); }
}

export inline void waitWRITE(int pipe, int timeout = -1) {
	struct pollfd pfd;
	pfd.fd		= pipe;
	pfd.events	= POLLOUT;
	pfd.revents = 0;
	if (poll(&pfd, 1, timeout) == -1) { throw std::runtime_error("poll failed"); }
}

export class PipeBuffer : public std::streambuf {
   public:
	explicit PipeBuffer(int pipe_fd) : pipe_fd(pipe_fd) {
		setg(buffer, buffer, buffer);
		setp(output_buffer, output_buffer + BUFFER_SIZE);
	}

	PipeBuffer(const PipeBuffer &)			  = delete;
	PipeBuffer &operator=(const PipeBuffer &) = delete;

	~PipeBuffer() override { sync(); }

	void close() {
		if (pipe_fd != -1) {
			if (sync() == -1) {
				dbLog(dbg::LOG_WARNING, "Failed to sync pipe buffer before closing: ", strerror(errno));
			}
			::close(pipe_fd);
			pipe_fd = -1;
		}
	}

	void setPipe(int pipe) { this->pipe_fd = pipe; }

   protected:
	int_type underflow() override {
		if (gptr() == egptr()) {
			ssize_t bytes_read = read(pipe_fd, buffer, BUFFER_SIZE);
			if (bytes_read <= 0) { throw std::runtime_error("failed to read from pipe"); }

			setg(buffer, buffer, buffer + bytes_read);
		}
		return traits_type::to_int_type(*gptr());
	}

	int_type overflow(int_type c = traits_type::eof()) override {
		if (c != traits_type::eof()) {
			if (pptr() == epptr()) { sync(); }
			*pptr() = traits_type::to_char_type(c);
			pbump(1);
		}
		return traits_type::not_eof(c);
	}

	int sync() override {
		ssize_t bytes_to_write = pptr() - pbase();
		if (bytes_to_write > 0) {
			ssize_t bytes_written = write(pipe_fd, output_buffer, bytes_to_write);
			if (bytes_written <= 0) {
				waitWRITE(pipe_fd);
				bytes_written = write(pipe_fd, output_buffer, bytes_to_write);
			}

			if (bytes_written < 0) {
				dbLog(dbg::LOG_WARNING, "Failed to write to pipe: ", strerror(errno));
				return -1;
			}

			// Reset the put pointer
			setp(output_buffer, output_buffer + BUFFER_SIZE);
		}
		return 0;
	}

   private:
	int	 pipe_fd;
	char buffer[BUFFER_SIZE];
	char output_buffer[BUFFER_SIZE];
};

export class Pipe {
   public:
	Pipe() : pipe(-1) {}
	Pipe(const Pipe &)			  = delete;
	Pipe &operator=(const Pipe &) = delete;
	Pipe(Pipe &&s) {
		this->pipe = s.pipe;
		s.pipe	   = -1;
	}
	Pipe &operator=(Pipe &&s) {
		this->pipe = s.pipe;
		s.pipe	   = -1;
		return *this;
	}

	Pipe(int pipe) : pipe(pipe) {}
	~Pipe() {
		if (this->pipe != -1) close(this->pipe);
	}

	operator int() const { return this->pipe; }

	void waitREAD(int timeout = -1) const { ::waitREAD(this->pipe, timeout); }
	void waitWRITE(int timeout = -1) const { ::waitWRITE(this->pipe, timeout); }

   private:
	int pipe = 0;
};

export class PipeStream : public std::iostream {
   public:
	PipeStream() : std::iostream(&buffer), buffer(-1), pipe(nullptr) {}
	PipeStream(const Pipe &s) : std::iostream(&buffer), buffer((int)s), pipe(&s) {}

	PipeStream(int pipe) : std::iostream(&buffer), buffer(pipe), pipe(nullptr) {}

	~PipeStream() override { this->flush(); }
	PipeStream &operator=(int pipe) {
		this->buffer.setPipe(pipe);
		return *this;
	}

	const Pipe &getpipe() { return *pipe; }
	void		close() {
		this->flush();
		buffer.close();
	}

   private:
	PipeBuffer	buffer;		// Our custom stream buffer
	const Pipe *pipe;
};

export class Process {
   public:
	Process(const Process &)			= delete;
	Process &operator=(const Process &) = delete;
	Process(Process &&s) {
		this->pid = s.pid;
		s.pid	  = -1;
	}
	Process &operator=(Process &&s) {
		this->pid = s.pid;
		s.pid	  = -1;
		return *this;
	}

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
	~Process() {
		if (pid != -1) { std::terminate(); }
	}

	int wait() {
		int status;
		waitpid(pid, &status, 0);
		pid = -1;
		return status;
	}

	PipeStream &in() { return in_stream; }
	PipeStream &out() { return out_stream; }
	PipeStream &err() { return err_stream; }

   private:
	pid_t	   pid;
	PipeStream in_stream, out_stream, err_stream;
};

export class ShellProcess : public Process {
   public:
	ShellProcess(const char *cmd) : Process("/bin/sh", "-c", cmd) {}
};
