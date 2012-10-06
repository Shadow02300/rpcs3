#include "stdafx.h"
#include "ELF64.h"

ELF64Loader::ELF64Loader(wxFile& f)
	: elf64_f(f)
	, LoaderBase()
{
}

ELF64Loader::ELF64Loader(const wxString& path)
	: elf64_f(*new wxFile(path))
	, LoaderBase()
{
}

bool ELF64Loader::LoadInfo()
{
	if(!elf64_f.IsOpened()) return false;

	if(!LoadEhdrInfo()) return false;
	if(!LoadPhdrInfo()) return false;
	if(!LoadShdrInfo()) return false;

	return true;
}

bool ELF64Loader::LoadData()
{
	if(!elf64_f.IsOpened()) return false;

	if(!LoadEhdrData()) return false;
	if(!LoadPhdrData()) return false;
	if(!LoadShdrData()) return false;

	return true;
}

bool ELF64Loader::Close()
{
	return elf64_f.Close();
}

bool ELF64Loader::LoadEhdrInfo()
{
	elf64_f.Seek(0);
	ehdr.Load(elf64_f);

	if(!ehdr.CheckMagic()) return false;

	if(ehdr.e_phentsize != sizeof(Elf64_Phdr))
	{
		ConLog.Error("elf64 error:  e_phentsize[0x%x] != sizeof(Elf64_Phdr)[0x%x]", ehdr.e_phentsize, sizeof(Elf64_Phdr));
		return false;
	}

	if(ehdr.e_shentsize != sizeof(Elf64_Shdr))
	{
		ConLog.Error("elf64 error: e_shentsize[0x%x] != sizeof(Elf64_Shdr)[0x%x]", ehdr.e_shentsize, sizeof(Elf64_Shdr));
		return false;
	}

	switch(ehdr.e_machine)
	{
	case MACHINE_PPC64:
	case MACHINE_SPU:
		machine = (Elf_Machine)ehdr.e_machine;
	break;

	default:
		machine = MACHINE_Unknown;
		ConLog.Error("Unknown elf64 type: 0x%x", ehdr.e_machine);
		return false;
	}

	entry = ehdr.GetEntry();
	if(entry == 0)
	{
		ConLog.Error("elf64 error: entry is null!");
		return false;
	}

	return true;
}

bool ELF64Loader::LoadPhdrInfo()
{
	phdr_arr.Clear();

	if(ehdr.e_phoff == 0 && ehdr.e_phnum)
	{
		ConLog.Error("LoadPhdr64 error: Program header offset is null!");
		return false;
	}

	elf64_f.Seek(ehdr.e_phoff);
	for(u32 i=0; i<ehdr.e_phnum; ++i)
	{
		Elf64_Phdr phdr;
		phdr.Load(elf64_f);
		phdr_arr.AddCpy(phdr);
	}

	return true;
}

bool ELF64Loader::LoadShdrInfo()
{
	shdr_arr.Clear();
	shdr_name_arr.Clear();
	if(ehdr.e_shoff == 0 && ehdr.e_shnum)
	{
		ConLog.Error("LoadShdr64 error: Section header offset is null!");
		return false;
	}

	elf64_f.Seek(ehdr.e_shoff);
	for(u32 i=0; i<ehdr.e_shnum; ++i)
	{
		Elf64_Shdr shdr;
		shdr.Load(elf64_f);
		shdr_arr.AddCpy(shdr);
	}

	if(ehdr.e_shstrndx >= shdr_arr.GetCount())
	{
		ConLog.Error("LoadShdr64 error: shstrndx too big!");
		return false;
	}

	for(u32 i=0; i<shdr_arr.GetCount(); ++i)
	{
		elf64_f.Seek(shdr_arr[ehdr.e_shstrndx].sh_offset + shdr_arr[i].sh_name);
		wxString name = wxEmptyString;
		while(!elf64_f.Eof())
		{
			char c;
			elf64_f.Read(&c, 1);
			if(c == 0) break;
			name += c;
		}

		shdr_name_arr.Add(name);
	}

	return true;
}

bool ELF64Loader::LoadEhdrData()
{
#ifdef LOADER_DEBUG
	ConLog.SkipLn();
	ehdr.Show();
	ConLog.SkipLn();
#endif
	return true;
}

bool ELF64Loader::LoadPhdrData()
{
	Memory.MemFlags.Clear();

	for(u32 i=0; i<phdr_arr.GetCount(); ++i)
	{
		phdr_arr[i].Show();

		if(phdr_arr[i].p_vaddr != phdr_arr[i].p_paddr)
		{
			ConLog.Warning
			( 
				"ElfProgram different load addrs: paddr=0x%8.8x, vaddr=0x%8.8x", 
				phdr_arr[i].p_paddr, phdr_arr[i].p_vaddr
			);
		}

		if(!Memory.IsGoodAddr(phdr_arr[i].p_vaddr, phdr_arr[i].p_memsz))
		{
			ConLog.Warning("Skipping...");
#ifdef LOADER_DEBUG
			ConLog.SkipLn();
#endif
			continue;
		}

		switch(phdr_arr[i].p_type)
		{
			case 0x00000001: //LOAD
				elf64_f.Seek(phdr_arr[i].p_offset);
				elf64_f.Read(&Memory[phdr_arr[i].p_vaddr], phdr_arr[i].p_filesz);
			break;

			case 0x00000007: //TLS
				Emu.SetTLSData(phdr_arr[i].p_vaddr, phdr_arr[i].p_filesz, phdr_arr[i].p_memsz);
			break;

			case 0x60000001: //LOOS+1
			{
				if(!phdr_arr[i].p_filesz) break;

				const sys_process_param& proc_param = *(sys_process_param*)&Memory[phdr_arr[i].p_vaddr];

				if(re(proc_param.size) < sizeof(sys_process_param))
				{
					ConLog.Warning("Bad proc param size! [0x%x : 0x%x]", re(proc_param.size), sizeof(sys_process_param));
				}
	
				if(re(proc_param.magic) != 0x13bcc5f6)
				{
					ConLog.Error("Bad magic! [0x%x]", Memory.Reverse32(proc_param.magic));
				}
				else
				{
					sys_process_param_info& info = Emu.GetInfo().GetProcParam();
					info.sdk_version = re(proc_param.info.sdk_version);
					info.primary_prio = re(proc_param.info.primary_prio);
					info.primary_stacksize = re(proc_param.info.primary_stacksize);
					info.malloc_pagesize = re(proc_param.info.malloc_pagesize);
					info.ppc_seg = re(proc_param.info.ppc_seg);
					//info.crash_dump_param_addr = re(proc_param.info.crash_dump_param_addr);
#ifdef LOADER_DEBUG
					ConLog.Write("*** sdk version: 0x%x", info.sdk_version);
					ConLog.Write("*** primary prio: %d", info.primary_prio);
					ConLog.Write("*** primary stacksize: 0x%x", info.primary_stacksize);
					ConLog.Write("*** malloc pagesize: 0x%x", info.malloc_pagesize);
					ConLog.Write("*** ppc seg: 0x%x", info.ppc_seg);
					//ConLog.Write("*** crash dump param addr: 0x%x", info.crash_dump_param_addr);
#endif
				}
			}
			break;

			case 0x60000002: //LOOS+2
			{
				if(!phdr_arr[i].p_filesz) break;

				sys_proc_prx_param proc_prx_param = *(sys_proc_prx_param*)&Memory[phdr_arr[i].p_vaddr];

				proc_prx_param.size = re(proc_prx_param.size);
				proc_prx_param.magic = re(proc_prx_param.magic);
				proc_prx_param.version = re(proc_prx_param.version);
				proc_prx_param.libentstart = re(proc_prx_param.libentstart);
				proc_prx_param.libentend = re(proc_prx_param.libentend);
				proc_prx_param.libstubstart = re(proc_prx_param.libstubstart);
				proc_prx_param.libstubend = re(proc_prx_param.libstubend);
				proc_prx_param.ver = re(proc_prx_param.ver);

#ifdef LOADER_DEBUG
				ConLog.Write("*** size: 0x%x", proc_prx_param.size);
				ConLog.Write("*** magic: 0x%x", proc_prx_param.magic);
				ConLog.Write("*** version: 0x%x", proc_prx_param.version);
				ConLog.Write("*** libentstart: 0x%x", proc_prx_param.libentstart);
				ConLog.Write("*** libentend: 0x%x", proc_prx_param.libentend);
				ConLog.Write("*** libstubstart: 0x%x", proc_prx_param.libstubstart);
				ConLog.Write("*** libstubend: 0x%x", proc_prx_param.libstubend);
				ConLog.Write("*** ver: 0x%x", proc_prx_param.ver);
#endif

				if(proc_prx_param.magic != 0x1b434cec)
				{
					ConLog.Error("Bad magic!");
				}
				else
				{	
					for(u32 s=proc_prx_param.libstubstart; s<proc_prx_param.libstubend; s+=sizeof(Elf64_StubHeader))
					{
						Elf64_StubHeader stub = *(Elf64_StubHeader*)Memory.GetMemFromAddr(s);

						stub.s_size = re(stub.s_size);
						stub.s_version = re(stub.s_version);
						stub.s_unk0 = re(stub.s_unk0);
						stub.s_unk1 = re(stub.s_unk1);
						stub.s_imports = re(stub.s_imports);
						stub.s_modulename = re(stub.s_modulename);
						stub.s_nid = re(stub.s_nid);
						stub.s_text = re(stub.s_text);

#ifdef LOADER_DEBUG
						ConLog.SkipLn();
						ConLog.Write("*** size: 0x%x", stub.s_size);
						ConLog.Write("*** version: 0x%x", stub.s_version);
						ConLog.Write("*** unk0: 0x%x", stub.s_unk0);
						ConLog.Write("*** unk1: 0x%x", stub.s_unk1);
						ConLog.Write("*** imports: %d", stub.s_imports);
						ConLog.Write("*** module name: %s [0x%x]", Memory.ReadString(stub.s_modulename), stub.s_modulename);
						ConLog.Write("*** nid: 0x%x", stub.s_nid);
						ConLog.Write("*** text: 0x%x", stub.s_text);
#endif

						for(u32 i=0; i<stub.s_imports; ++i)
						{
							const u32 nid = Memory.Read32(stub.s_nid + i*4);
							const u32 text = Memory.Read32(stub.s_text + i*4);
#ifdef LOADER_DEBUG
							ConLog.Write("import %d:", i+1);
							ConLog.Write("*** nid: 0x%x", nid);
							ConLog.Write("*** text: 0x%x", text);
#endif
							Memory.MemFlags.Add(text, stub.s_text + i*4, nid);
						}
					}
#ifdef LOADER_DEBUG
					ConLog.SkipLn();
#endif
				}
			}
			break;
		}
#ifdef LOADER_DEBUG
		ConLog.SkipLn();
#endif
	}

	return true;
}

bool ELF64Loader::LoadShdrData()
{
	for(uint i=0; i<shdr_arr.GetCount(); ++i)
	{
		Elf64_Shdr& shdr = shdr_arr[i];

		if(i < shdr_name_arr.GetCount())
		{
			const wxString& name = shdr_name_arr[i];

#ifdef LOADER_DEBUG
			ConLog.Write("Name: %s", shdr_name_arr[i]);
#endif
		}

#ifdef LOADER_DEBUG
		shdr.Show();
		ConLog.SkipLn();
#endif
		if((shdr.sh_flags & SHF_ALLOC) != SHF_ALLOC) continue;

		const u64 addr = shdr.sh_addr;
		const u64 size = shdr.sh_size;

		if(size == 0 || !Memory.IsGoodAddr(addr, size)) continue;

		switch(shdr.sh_type)
		{
		case SHT_NOBITS:
			memset(&Memory[addr], 0, size);
		break;

		case SHT_PROGBITS:
			/*
			elf64_f.Seek(shdr.sh_offset);
			elf64_f.Read(&Memory[addr], shdr.sh_size);
			*/
		break;
		}
	}

	return true;
}