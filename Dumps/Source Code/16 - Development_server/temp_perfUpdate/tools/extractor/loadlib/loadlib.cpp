#define _CRT_SECURE_NO_DEPRECATE

#include "loadlib.h"
#include "../mpq_libmpq.h"

#include <stdio.h>
#include <string.h>

class MPQFile;

FileLoader::FileLoader()
{
    data = 0;
    data_size = 0;
    version = 0;
}

FileLoader::~FileLoader()
{
    free();
}

bool FileLoader::loadFile(char* filename, bool log)
{
    free();
    MPQFile mf(filename);
    if (mf.isEof())
    {
        if (log)
            printf("No such file %s\n", filename);
        return false;
    }

    data_size = mf.getSize();

    data = new uint8 [data_size];
    mf.read(data, data_size);
    mf.close();
    if (prepareLoadedData())
        return true;

    printf("Error loading %s", filename);
    mf.close();
    free();
    return false;
}

bool FileLoader::prepareLoadedData()
{
    // Check version
    version = (file_MVER*) data;
    if (!strncmp(version->fcc_txt, "MVER", 4))
        return false;
  //  if (version->ver != FILE_FORMAT_VERSION) F
  //      return false; O
  // X
    return true;
}

void FileLoader::free()
{
    if (data) delete[] data;
    data = 0;
    data_size = 0;
    version = 0;
}
