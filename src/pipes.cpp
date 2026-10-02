#include "pipes.hpp"
#include "dbg.hpp"
#include "debug_macros.hpp"

void waitREAD(int pipe, int timeout) {
	struct pollfd pfd;
	pfd.fd		= pipe;
	pfd.events	= POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, timeout) == -1) { throw std::runtime_error("poll failed"); }
}

void waitWRITE(int pipe, int timeout) {
	struct pollfd pfd;
	pfd.fd		= pipe;
	pfd.events	= POLLOUT;
	pfd.revents = 0;
	if (poll(&pfd, 1, timeout) == -1) { throw std::runtime_error("poll failed"); }
}

PipeBuffer::PipeBuffer(int pipe_fd) : pipe_fd(pipe_fd) {
	setg(buffer, buffer, buffer);
	setp(output_buffer, output_buffer + BUFFER_SIZE);
}

PipeBuffer::~PipeBuffer() { sync(); }

void PipeBuffer::close() {
	if (pipe_fd != -1) {
		if (sync() == -1) { dbLog(dbg::LOG_WARNING, "Failed to sync pipe buffer before closing: ", strerror(errno)); }
		::close(pipe_fd);
		pipe_fd = -1;
	}
}

void PipeBuffer::setPipe(int pipe) { this->pipe_fd = pipe; }

PipeBuffer::int_type PipeBuffer::underflow() {
	if (gptr() == egptr()) {
		ssize_t bytes_read = read(pipe_fd, buffer, BUFFER_SIZE);
		if (bytes_read <= 0) { throw std::runtime_error("failed to read from pipe"); }

		setg(buffer, buffer, buffer + bytes_read);
	}
	return traits_type::to_int_type(*gptr());
}

PipeBuffer::int_type PipeBuffer::overflow(int_type c) {
	if (c != traits_type::eof()) {
		if (pptr() == epptr()) { sync(); }
		*pptr() = traits_type::to_char_type(c);
		pbump(1);
	}
	return traits_type::not_eof(c);
}

int PipeBuffer::sync() {
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

Pipe::Pipe() : pipe(-1) {}
Pipe::Pipe(Pipe &&s) {
	this->pipe = s.pipe;
	s.pipe	   = -1;
}
Pipe &Pipe::operator=(Pipe &&s) {
	this->pipe = s.pipe;
	s.pipe	   = -1;
	return *this;
}

Pipe::Pipe(int pipe) : pipe(pipe) {}
Pipe::~Pipe() {
	if (this->pipe != -1) close(this->pipe);
}

Pipe::operator int() const { return this->pipe; }

void Pipe::waitREAD(int timeout) const { ::waitREAD(this->pipe, timeout); }
void Pipe::waitWRITE(int timeout) const { ::waitWRITE(this->pipe, timeout); }

PipeStream::PipeStream() : std::iostream(&buffer), buffer(-1), pipe(nullptr) {}
PipeStream::PipeStream(const Pipe &s) : std::iostream(&buffer), buffer((int)s), pipe(&s) {}

PipeStream::PipeStream(int pipe) : std::iostream(&buffer), buffer(pipe), pipe(nullptr) {}

PipeStream::~PipeStream() { this->flush(); }
PipeStream &PipeStream::operator=(int pipe) {
	this->buffer.setPipe(pipe);
	return *this;
}

const Pipe &PipeStream::getpipe() { return *pipe; }
void		PipeStream::close() {
	this->flush();
	buffer.close();
}

Process::Process(Process &&s) {
	this->pid = s.pid;
	s.pid	  = -1;
}
Process &Process::operator=(Process &&s) {
	this->pid = s.pid;
	s.pid	  = -1;
	return *this;
}

Process::~Process() {
	if (pid != -1) { std::terminate(); }
}

int Process::wait() {
	int status;
	waitpid(pid, &status, 0);
	pid = -1;
	return status;
}

PipeStream &Process::in() { return in_stream; }
PipeStream &Process::out() { return out_stream; }
PipeStream &Process::err() { return err_stream; }

ShellProcess::ShellProcess(const char *cmd) : Process("/bin/sh", "-c", cmd) {}
