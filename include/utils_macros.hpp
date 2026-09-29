#pragma once

#define JOB(name, ...)                     \
	static int _job_##name = []() -> int { \
		__VA_ARGS__;                       \
		return 0;                          \
	}();

#define BENCH(x, n, s)                                                                                    \
	{                                                                                                     \
		auto t1 = std::chrono::high_resolution_clock::now();                                              \
		for (int i = 0; i < n; ++i)                                                                       \
			x;                                                                                            \
		auto t2	  = std::chrono::high_resolution_clock::now();                                            \
		auto diff = t2 - t1;                                                                              \
		std::cout << s << std::chrono::duration_cast<std::chrono::microseconds>(diff / n).count() << "μs" \
				  << std::endl;                                                                           \
	}

#define STR(x) #x
