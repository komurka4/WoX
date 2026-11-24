/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 * Modified in 2026 for WoX: C stdio in place of std::ifstream.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* MT32Emu::FileStream for xeen_mt32.dll (src/audio/MT32.cmake). Munt's FileStream.h declares the
 * class around a std::ifstream, which with a statically linked C++ runtime puts the whole
 * iostream and locale machinery (about 800 KB) into the DLL. This header is force-included
 * before every Munt translation unit and takes Munt's include guard, so all of them see this
 * declaration: the same public interface, held by a C stdio FILE. Munt's FileStream.cpp is not
 * built; mt32_file_stream.cpp implements the class.
 */
#ifndef MT32EMU_FILE_STREAM_H
#define MT32EMU_FILE_STREAM_H

#include <cstdio>

#include "globals.h"
#include "Types.h"
#include "File.h"

namespace MT32Emu {

class FileStream : public AbstractFile {
public:
	MT32EMU_EXPORT FileStream();
	MT32EMU_EXPORT ~FileStream();
	MT32EMU_EXPORT size_t getSize();
	MT32EMU_EXPORT const Bit8u *getData();
	MT32EMU_EXPORT bool open(const char *filename);
	MT32EMU_EXPORT void close();

private:
	std::FILE *file;
	const Bit8u *data;
	size_t size;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_FILE_STREAM_H
