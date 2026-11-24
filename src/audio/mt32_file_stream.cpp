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

/* Munt 2.8.3's FileStream (src/FileStream.cpp) with C stdio in place of std::ifstream; see
 * mt32_file_stream.h. The behaviour follows the original member by member: the size is measured
 * once by seeking to the end, getData reads the whole file once, keeps the bytes and closes the
 * file, and a short read is a failure. The narrow file name goes to fopen as it went to
 * std::ifstream::open (the C runtime's code page on Windows).
 */
#include "internals.h"

#include "mt32_file_stream.h"

namespace MT32Emu {

FileStream::FileStream() : file(NULL), data(NULL), size(0)
{}

FileStream::~FileStream() {
	close();
	delete[] data;
}

size_t FileStream::getSize() {
	if (size != 0) {
		return size;
	}
	if (file == NULL) {
		return 0;
	}
	if (std::fseek(file, 0, SEEK_END) != 0) {
		return 0;
	}
	const long end = std::ftell(file);
	size = end > 0 ? size_t(end) : 0;
	return size;
}

const Bit8u *FileStream::getData() {
	if (data != NULL) {
		return data;
	}
	if (file == NULL) {
		return NULL;
	}
	if (getSize() == 0) {
		return NULL;
	}
	Bit8u *fileData = new Bit8u[size];
	if (std::fseek(file, 0, SEEK_SET) != 0 || std::fread(fileData, 1, size, file) != size) {
		delete[] fileData;
		return NULL;
	}
	data = fileData;
	close();
	return data;
}

bool FileStream::open(const char *filename) {
	close();
	file = std::fopen(filename, "rb");
	return file != NULL;
}

void FileStream::close() {
	if (file != NULL) {
		std::fclose(file);
		file = NULL;
	}
}

} // namespace MT32Emu
