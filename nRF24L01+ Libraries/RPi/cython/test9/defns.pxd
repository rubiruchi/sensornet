cdef extern from "ar.h":
	char addr[4]
	void pop()
	char getElement(int n)
	void putElement(int n, char c)
	void putAll(int n, int c[])
	char *getAll()
	void printAll()
