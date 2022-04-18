// List of tests to be skipped on ndtest
//
// Append new test cases to this array below until support is added on ndtest.
//
["clear.sh",		// No error injection support on PPC.
 "daxdev-errors.sh",	// 		""
 "inject-error.sh",	// 		""
 "pfn-meta-errors.sh",  //		""
 "pmem-errors.sh",	//		""
 "btt-errors.sh",	//		""
 "label-compat.sh",	// Legacy namespace support test/irrelavent on
			// ndtest.
 "security.sh",		// No support on PPC yet.
 "daxctl-create.sh",	// Depends on dax_hmem
 "sub-section.sh",	// Tests using nd_e820, either duplication when
			// running on INTEL host, or cannot be tested on
			// PPC host.
 "dax-dev",		//		""
 "device-dax",		//		""
 "device-dax-fio.sh",	//		""
 "dax-ext4.sh",		//		""
 "dax-xfs.sh",		//		""
 "daxctl-devices.sh",	//		""
 "revoke_devmem",	//		""
 "align.sh",		//		""
 "dm.sh",		//		""
 "mmap.sh",		//		""
 "monitor.sh"		// To be fixed
]

// NOTE: The libjson-c doesn't like comments in json files, so keep the file
// extension as .js to pacify.
