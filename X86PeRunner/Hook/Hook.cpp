#include"Hook.h"

#include"Emulator\Emulator.h"
#include"Emulator\X86Emulator.h"

#include"CallConverter\StdcallCallConverter.h"
#include"CallConverter\CallConverter.h"

#include"PreparedHook\Kernel32.h"

#include<map>


Emulator* emulator;
CallConverter* callConverter;


#define HOOK_PROC_ADDRESS(pe,func) HookProcAddress(pe, "##func##" , (DWORD)PreparedHook::##func)
void HookProcAddress(PE_HANDLE pe, LPCSTR lpProcName, DWORD newProcAddress)
{
	auto hooks = (std::map<std::string, ImportHookData*>*)pe->Data;
	auto oldProcAddress = GetProcAddress((HMODULE)pe->Base, lpProcName);

	uc_hook* hh = new uc_hook;

	auto data = new ImportHookData;
	data->Dll = pe;
	data->ImportName = lpProcName;
	data->ByName = true;
	data->Proc = oldProcAddress;
	data->Hook = hh;

	uc_hook_add(emulator->engine, hh, UC_HOOK_CODE, (void*)newProcAddress, (void*)data, (uint64_t)oldProcAddress, (uint64_t)oldProcAddress);

	hooks->insert(std::make_pair(lpProcName, data));
}

void InitHooks()
{
	emulator = new X86Emulator;
	callConverter = new StdcallCallConverter;

	//Hook LoadLibrary and others
	PE_HANDLE HKernel32 = PeLdrLoadModuleA("Kernel32.dll", ExecMainCallback, ImportCallback);
	auto *hooks = new std::map<std::string, ImportHookData*>;
	HKernel32->Data = hooks;
	
	//HookProcAddress(HKernel32, "GetModuleFileNameW", (DWORD)PreparedHook::GetModuleFileNameW);
	HOOK_PROC_ADDRESS(HKernel32, GetModuleFileNameW);


}

void ExecMainCallback(PE_HANDLE Pe)
{
	DWORD dwEntryPoint = PeLdrGetEntryPoint(Pe);

	if (Pe->IsNative)
	{
		if (Pe->IsExe)
		{
			int(*entryPoint)();
			entryPoint = (int(*)())dwEntryPoint;

			int ret = entryPoint();
		}
		else
		{
			BOOL(WINAPI *dllMain)(HINSTANCE hModule, DWORD dwReason, LPVOID);
			dllMain = (BOOL(WINAPI *)(HINSTANCE hModule, DWORD dwReason, LPVOID))dwEntryPoint;

			BOOL ret = dllMain((HMODULE)Pe->Base, DLL_PROCESS_ATTACH, NULL);
		}
	}
	else
	{
		if (Pe->IsExe)
		{
			emulator->StackPush(dwEntryPoint+1);
			emulator->Start(dwEntryPoint, dwEntryPoint+1);

			int ret = emulator->RegRead(UC_X86_REG_EAX);
		}
		else
		{
			emulator->StackPush(NULL);//LPVOID
			emulator->StackPush(DLL_PROCESS_ATTACH);//DWORD dwReason
			emulator->StackPush((int)Pe->Base);//HMODULE

			emulator->StackPush(dwEntryPoint -2);
			emulator->Start(dwEntryPoint, dwEntryPoint -2);

			BOOL ret = emulator->RegRead(UC_X86_REG_EAX);
		}
	}
}

FARPROC ImportCallback(PE_HANDLE Pe, PE_HANDLE NeededDll, LPCSTR ImportName, BOOL ByName)
{
	/*
	PE_HANDLE Pe		:	��Ҫָ�����뺯����PE�ļ������类�򿪵�exe��Peһ����������Unicornģ�����ϵġ�
	PE_HANDLE NeededDll	:	ָ��Ҫ�������PE(Dll)�ļ���
	NeededDll����Ƿ�Native����Ҫ������Unicornģ�����ϣ��ģ���ֱ�ӷ��ص����ַ��ȫ������ģ�����ڵ�PE�ļ��Ǵ���
	�����Native�ģ���hook�����ַ�������е������ַʱֹͣ��Native���ú�����ע����Ҫ�޸������������⣩
	LPCSTR ImportName	:	������ĺ�������Ҳ���������
	BOOL ByName			:	Ϊtrue��ImportName��һ��char*��Ϊfalse��ImportNameʵ��һ��������ţ�PE�涨��
	*/

	FARPROC proc;
	if (NeededDll->IsNative)
	{
		proc = GetProcAddress((HMODULE)NeededDll->Base, ImportName);

		if (proc == NULL)
		{
			printf("Hook fail!Master dll:%s\tSlave dll:%s\tImportName:%s\n", Pe->FileName, NeededDll->FileName, ImportName);
		}
		std::map<std::string, ImportHookData*> *hooks;
		if (NeededDll->Data == nullptr)
		{
			hooks = new std::map<std::string, ImportHookData*>();
			NeededDll->Data = hooks;
		}
		else
		{
			hooks = (std::map<std::string, ImportHookData*>*)NeededDll->Data;
		}

		if (hooks->find(ImportName) == hooks->end())
		{
			uc_hook* hh = new uc_hook;

			auto data = new ImportHookData;
			data->Dll = NeededDll;
			data->ImportName = ImportName;
			data->ByName = ByName;
			data->Proc = proc;
			data->Hook = hh;

			void UnicornHookCallback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
			uc_hook_add(emulator->engine, hh, UC_HOOK_CODE, UnicornHookCallback, (void*)data, (uint64_t)proc, (uint64_t)proc);

			hooks->insert(std::make_pair(ImportName, data));
		}

	}
	else
	{
		proc = PeLdrGetProcAddressA(NeededDll, ImportName, ExecMainCallback, ImportCallback);
	}

	return proc;

}

void UnicornHookCallback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data)
{
	ImportHookData* data = (ImportHookData*)user_data;

	callConverter->Call(emulator, data);

	int pc = emulator->StackPop();
	emulator->RegWrite(UC_X86_REG_EIP, pc);
}