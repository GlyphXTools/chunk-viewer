#ifndef FILE_H
#define FILE_H

#include <string>

class File
{
	unsigned long refcount;
	std::wstring   name;

protected:
	virtual ~File() {}

public:
	void AddRef();
	void Release();

	const std::wstring getName()   const { return name; }
	virtual bool              eof() = 0;
	virtual unsigned long     getSize() = 0;
	virtual void              seek(unsigned long offset) = 0;
	virtual unsigned long     tell() = 0;
	virtual unsigned long     read( void* buffer, unsigned long count ) = 0;

	File(const std::wstring& name);
};

class PhysicalFile : public File
{
	struct Handle;

	Handle*       handle;
	unsigned long offset;

	~PhysicalFile();
public:
	bool               eof();
	unsigned long      getSize();
	void               seek(unsigned long offset);
	unsigned long      tell();
	unsigned long      read( void* buffer, unsigned long count );

	PhysicalFile(PhysicalFile& file);
	PhysicalFile(const std::wstring& filename);
	PhysicalFile();
};

class SubFile : public File
{
	File*         file;
	unsigned long offset;
	unsigned long start;
	unsigned long size;

	~SubFile();
public:
	bool          eof();
	unsigned long getSize();
	void          seek(unsigned long offset);
	unsigned long tell();
	unsigned long read( void* buffer, unsigned long count );

	SubFile(File* file, const std::wstring& filename, unsigned long start, unsigned long size);
};

#endif