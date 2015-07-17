// http://www.apache.org/licenses/LICENSE-2.0
// Copyright 2014 Perttu Ahola <celeron55@gmail.com>
#pragma once

namespace bdebug
{
	extern "C" {
		void cmem_libc_enable();
		void cmem_libc_disable();
	}
}
// vim: set noet ts=4 sw=4:
