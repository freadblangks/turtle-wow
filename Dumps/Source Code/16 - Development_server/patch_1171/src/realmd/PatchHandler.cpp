/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/** \file
  \ingroup realmd
  */

#include "AuthCodes.h"
#include "Log.h"
#include "Common.h"
#include "Timer.h"
#include "PatchHandler.h"
#include "PatchLimiter.hpp"

#ifdef WIN32
#include <filesystem>
#endif

#include <ace/OS_NS_sys_socket.h>
#include <ace/OS_NS_dirent.h>
#include <ace/OS_NS_errno.h>
#include <ace/OS_NS_unistd.h>


#include <ace/os_include/netinet/os_tcp.h>

#include "Policies/SingletonImp.h"
#include "Policies/ThreadingModel.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

PatchLimiter sPatchLimiter;

extern int32 PatchHandlerKBytesDownloadLimit;

struct Chunk
{
    ACE_UINT8 cmd;
    ACE_UINT16 data_size;
    ACE_UINT8 data[4096]; // 4096 - page size on most arch
};

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif

PatchHandler::PatchHandler(ACE_HANDLE socket, ACE_HANDLE patch)
{
    reactor(nullptr);
    set_handle(socket);
    patch_fd_ = patch;

	LastUpdateMs = WorldTimer::getMSTime();
	SecondLimitBytes = PatchHandlerKBytesDownloadLimit * 1024;
}

PatchHandler::~PatchHandler()
{
    if(patch_fd_ != ACE_INVALID_HANDLE)
        ACE_OS::close(patch_fd_);
}

int PatchHandler::open(void*)
{
    if(get_handle() == ACE_INVALID_HANDLE || patch_fd_ == ACE_INVALID_HANDLE)
        return -1;

    int nodelay = 0;
    if (-1 == peer().set_option(ACE_IPPROTO_TCP,
                TCP_NODELAY,
                &nodelay,
                sizeof(nodelay)))
    {
        return -1;
    }

#if defined(TCP_CORK)
    int cork = 1;
    if (-1 == peer().set_option(ACE_IPPROTO_TCP,
                TCP_CORK,
                &cork,
                sizeof(cork)))
    {
        return -1;
    }
#endif //TCP_CORK

    (void) peer().disable(ACE_NONBLOCK);

    return activate(THR_NEW_LWP | THR_DETACHED | THR_INHERIT_SCHED);
}

int PatchHandler::svc(void)
{
    // Do 1 second sleep, similar to the one in game/WorldSocket.cpp
    // Seems client have problems with too fast sends.
    ACE_OS::sleep(1);

    int flags = MSG_NOSIGNAL;

    Chunk data;
    data.cmd = CMD_XFER_DATA;

    ssize_t r;

    while((r = ACE_OS::read(patch_fd_, data.data, sizeof(data.data))) > 0)
    {
        data.data_size = (ACE_UINT16)r;

        auto size = ((size_t)r) + sizeof(data) - sizeof(data.data);
        while (!sPatchLimiter.IsAllowed(size))
        {
            ACE_Time_Value SleepValue;
            SleepValue.set_msec(100u);
            ACE_OS::sleep(SleepValue);
        }

		ssize_t sendedBytes = peer().send((const char*)&data,
			size,
			flags);

		if (sendedBytes == -1)
		{
			return -1;
		}


		SecondLimitBytes -= sendedBytes;

		if (SecondLimitBytes <= 0)
		{
			// check for time limit now
			uint32 Diff = 0;
			do 
			{
				uint32 CurrentTime = WorldTimer::getMSTime();
				Diff = CurrentTime - LastUpdateMs;
				if (Diff > 1000)
				{
					SecondLimitBytes = PatchHandlerKBytesDownloadLimit * 1024;
					LastUpdateMs = CurrentTime;
				}
				else
				{
					ACE_Time_Value SleepValue;
					SleepValue.set_msec(100u);
					ACE_OS::sleep(SleepValue);
				}
			} while (Diff < 1000);
		}
    }

    if(r == -1)
    {
        return -1;
    }

    return 0;
}

PatchCache::~PatchCache()
{
    for (Patches::iterator i = patches_.begin (); i != patches_.end (); i++)
        delete i->second;
}

PatchCache::PatchCache()
{
    LoadPatchesInfo();
}

using PatchCacheLock = MaNGOS::ClassLevelLockable<PatchCache, std::mutex>;
INSTANTIATE_SINGLETON_2(PatchCache, PatchCacheLock);
INSTANTIATE_CLASS_MUTEX(PatchCache, std::mutex);

PatchCache* PatchCache::instance()
{
    return &MaNGOS::Singleton<PatchCache, PatchCacheLock>::Instance();
}

void PatchCache::LoadPatchMD5(const char* szFileName)
{
    // Try to open the patch file
    std::string path = szFileName;
    FILE* pPatch = fopen(path.c_str(), "rb");
    DEBUG_LOG("Loading patch info from file %s", path.c_str());

    if(!pPatch)
        return;

    // Calculate the MD5 hash
    MD5_CTX ctx;
    MD5_Init(&ctx);

    const size_t check_chunk_size = 4*1024;

    ACE_UINT8 buf[check_chunk_size];

    while(!feof (pPatch))
    {
        size_t read = fread(buf, 1, check_chunk_size, pPatch);
        MD5_Update(&ctx, buf, read);
    }

    fclose(pPatch);

    // Store the result in the internal patch hash map
    patches_[path] = new PATCH_INFO;
    MD5_Final((ACE_UINT8 *) & patches_[path]->md5, &ctx);
}

bool PatchCache::GetHash(const char * pat, ACE_UINT8 mymd5[MD5_DIGEST_LENGTH])
{
    for (Patches::iterator i = patches_.begin (); i != patches_.end (); i++)
        if (!stricmp(pat, i->first.c_str ()))
        {
            memcpy(mymd5, i->second->md5, MD5_DIGEST_LENGTH);
            return true;
        }

    return false;
}

#ifdef WIN32
#define fssystem std::filesystem
#endif

void PatchCache::LoadPatchesInfo()
{
#ifdef WIN32
	fssystem::path PatchesDir = "./patches/";

	if (!fssystem::exists(PatchesDir))
	{
		return;
	}

	fssystem::directory_iterator iter(PatchesDir);

	for (const fssystem::directory_entry& DirEntry : fssystem::directory_iterator(PatchesDir))
	{
		const fssystem::path& filePath = DirEntry.path();
		fssystem::path clearFilename = filePath.filename();
		std::string strClearFilename = clearFilename.string();

		if (strClearFilename.size() < 8)
		{
			continue;
		}

		if (clearFilename.extension().compare("mpq"))
		{
			LoadPatchMD5(strClearFilename.c_str());
		}
	}
#else
    std::string path = sConfig.GetStringDefault("PatchesDir", "./patches") + "/";
    std::string fullpath;
    ACE_DIR* dirp = ACE_OS::opendir(ACE_TEXT(path.c_str()));
    DEBUG_LOG("Loading patch info from folder %s", path.c_str());

	if (!dirp)
		return;

	ACE_DIRENT* dp;

	while ((dp = ACE_OS::readdir(dirp)) != nullptr)
	{
		int l = strlen(dp->d_name);
		if (l < 8)
			continue;

        if (!memcmp(&dp->d_name[l - 4], ".mpq", 4))
        {
            fullpath = path + dp->d_name;
            LoadPatchMD5(fullpath.c_str());
}
	}

	ACE_OS::closedir(dirp);
#endif
}
