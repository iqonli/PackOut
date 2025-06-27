#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <Windows.h>
#include <filesystem>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cctype>
#include <regex>
#define i_input _color(14);cout<<'>';_color();
namespace fs = std::filesystem;
using namespace std;

void _color(int __c=7)//着色
{
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), __c);
}

// 封装执行命令的函数
bool windCMD(const string& command, bool wait = true, DWORD creationFlags = CREATE_NO_WINDOW)
{
	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };

	// 创建可修改的命令行副本
	char* cmdLine = new char[command.size() + 1];
	strcpy_s(cmdLine, command.size() + 1, command.c_str());

	BOOL result = CreateProcessA(
	                  NULL,                   // 应用程序名
	                  cmdLine,                // 命令行
	                  NULL,                   // 进程安全属性
	                  NULL,                   // 线程安全属性
	                  FALSE,                  // 不继承句柄
	                  creationFlags,          // 创建标志
	                  NULL,                   // 环境块
	                  NULL,                   // 当前目录
	                  &si,                    // 启动信息
	                  &pi                     // 进程信息
	              );

	delete[] cmdLine;

	if (!result)
	{
		DWORD error = GetLastError();
		_color(12);
		cout << "执行命令失败! 错误代码: " << error << endl;
		_color();
		return false;
	}

	// 按需等待进程结束
	if (wait)
	{
		WaitForSingleObject(pi.hProcess, INFINITE);
	}

	// 必须关闭句柄防止泄漏
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return true;
}

// 文件列表结构
struct FILELIST
{
	vector<string> folders;
	vector<string> files;
};

// 获取当前时间字符串
string getCurrentTimeString()
{
	auto now = chrono::system_clock::now();
	auto in_time_t = chrono::system_clock::to_time_t(now);

	tm tm;
	localtime_s(&tm, &in_time_t);

	ostringstream oss;
	oss << put_time(&tm, "%Y%m%d_%H%M%S");
	return oss.str();
}

// 生成文件名
string generateFileName(const string& prefix)
{
	return prefix + "_" + getCurrentTimeString() + ".txt";
}

// 获取目录列表
FILELIST getDirectoryList(const string& targetDir)
{
	FILELIST result;

	try
	{
		for (const auto& entry : fs::directory_iterator(targetDir))
		{
			if (entry.is_directory())
			{
				result.folders.push_back(entry.path().filename().string());
			}
			else
			{
				result.files.push_back(entry.path().filename().string());
			}
		}
	}
	catch (const fs::filesystem_error& e)
	{
		_color(12);
		cout << "获取目录列表错误: " << e.what() << endl;
		_color();
	}

	return result;
}

// 生成xcopy脚本
string generateXcopyCMD(const string& sourceDir,
                        const string& targetDir,
                        int overwriteOption,
                        int autoDelete)
{
	// 验证参数有效性
	if (overwriteOption < 0 || overwriteOption > 2)
	{
		throw invalid_argument("覆盖选项必须为0(询问),1(不覆盖)或2(覆盖)");
	}
	if (autoDelete < 0 || autoDelete > 1)
	{
		throw invalid_argument("自动删除选项必须为0(不删除)或1(删除)");
	}

	// 映射覆盖选项到xcopy参数
	string overwriteFlag;
	switch (overwriteOption)
	{
		case 0:
			overwriteFlag = "";
			break;     // 默认行为(询问)
		case 1:
			overwriteFlag = "/-Y";
			break;  // 不覆盖
		case 2:
			overwriteFlag = "/Y";
			break;   // 覆盖
	}

	ostringstream script;

	// 添加脚本头 - 静默模式
	script << "@echo off\n";
	script << "setlocal enabledelayedexpansion\n";

	// 使用xcopy复制整个文件夹
	string commonFlags = "/E /I /C /H /K /Q " + overwriteFlag;
	string source = "\"" + sourceDir + "\\*\"";
	string target = "\"" + targetDir + "\"";

	// 主复制命令
	script << "echo 正在复制 " << sourceDir << " 到 " << targetDir << "...\n";
	script << "xcopy " << source << " " << target << " " << commonFlags << "\n";

	// 检查复制是否成功
	script << "if %errorlevel% neq 0 (\n";
	script << "   echo 复制过程中发生错误，错误代码: %errorlevel%\n";
	script << "   exit /b %errorlevel%\n";
	script << ")\n";

	// 删除源文件命令（仅在移动操作时执行）
	if (autoDelete == 1)
	{
		script << "echo 删除源目录...\n";
		script << "rmdir /S /Q \"" << sourceDir << "\"\n";
		script << "if %errorlevel% neq 0 (\n";
		script << "   echo 删除源目录时发生错误\n";
		script << "   exit /b %errorlevel%\n";
		script << ")\n";
	}

	// 添加脚本尾
	script << "echo 操作成功完成!\n";
	script << "endlocal\n";
	script << "exit /b 0\n";

	return script.str();
}

// 创建临时文件名
string createTempFilename()
{
	char tempPath[MAX_PATH];
	char tempFile[MAX_PATH];

	GetTempPathA(MAX_PATH, tempPath);
	GetTempFileNameA(tempPath, "MOVE", 0, tempFile);

	return string(tempFile) + ".bat";
}

// 验证路径是否存在
bool directoryExists(const string& path)
{
	DWORD attrib = GetFileAttributesA(path.c_str());
	return (attrib != INVALID_FILE_ATTRIBUTES &&
	        (attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// 标准化路径分隔符
string standardPath(string path)
{
	for (char& c : path)
	{
		if (c == '/') c = '\\';
	}
	return path;
}

// 检查是否为根目录
bool isRootDirectory(const string& path)
{
	// 匹配盘符根目录 (如 C:\, D:\) 和UNC根目录 (如 \\server\share\)
	regex rootPattern(R"(^[a-zA-Z]:\\$|^\\\\[^\\]+\\[^\\]+\\?$)");
	return regex_match(path, rootPattern);
}

// 获取父目录
string getParentDirectory(const string& path)
{
	// 检查是否是根目录
	if (isRootDirectory(path))
	{
		throw runtime_error("无法获取根目录的父目录");
	}

	// 查找最后一个反斜杠
	size_t lastBackslash = path.find_last_of('\\');
	if (lastBackslash == string::npos)
	{
		throw runtime_error("路径中未找到反斜杠");
	}

	// 处理特殊情况: 如果路径以反斜杠结尾 (非根目录)
	string temp = path;
	if (temp.back() == '\\')
	{
		temp.pop_back();
		lastBackslash = temp.find_last_of('\\');
		if (lastBackslash == string::npos)
		{
			throw runtime_error("路径格式无效");
		}
	}

	// 提取父目录
	string parent = temp.substr(0, lastBackslash);

	// 确保父目录有效
	if (parent.empty())
	{
		throw runtime_error("提取的父目录为空");
	}

	// 对于盘符路径 (如 "C:"), 添加反斜杠使其成为根目录
	if (parent.size() == 2 && parent[1] == ':')
	{
		parent += '\\';
	}

	return parent;
}

int main(int argc, char* argv[])
{
	string sourceDir, targetDir;
	int overwriteOption = -1; // 初始化为-1表示未设置
	int autoDelOption = -1;   // 初始化为-1表示未设置

	// 处理命令行参数
	if (argc > 1)
	{
		sourceDir = argv[1];
		// 删除首尾双引号
		if (!sourceDir.empty() && sourceDir.front() == '"')
		{
			sourceDir.erase(0, 1);
		}
		if (!sourceDir.empty() && sourceDir.back() == '"')
		{
			sourceDir.pop_back();
		}
		cout << "From COMMAND: " << sourceDir << endl;

		// 如果有第二个参数（覆盖选项）
		if (argc > 2)
		{
			try
			{
				overwriteOption = stoi(argv[2]);
				if (overwriteOption < 0 || overwriteOption > 2)
				{
					cout << "警告: 覆盖选项无效, 将使用默认值" << endl;
					overwriteOption = -1; // 标记为需要输入
				}
			}
			catch (...)
			{
				cout << "警告: 覆盖选项无效, 将使用默认值" << endl;
				overwriteOption = -1; // 标记为需要输入
			}
		}

		// 如果有第三个参数（自动删除选项）
		if (argc > 3)
		{
			try
			{
				autoDelOption = stoi(argv[3]);
				if (autoDelOption < 0 || autoDelOption > 1)
				{
					cout << "警告: 自动删除选项无效, 将使用默认值" << endl;
					autoDelOption = -1; // 标记为需要输入
				}
			}
			catch (...)
			{
				cout << "警告: 自动删除选项无效, 将使用默认值" << endl;
				autoDelOption = -1; // 标记为需要输入
			}
		}
	}
	else
	{
st1:
		_color(10);
		cout << "PackOut 拆包\nby IQ Online Studio, github.com/iqonli\n";
		_color();
		cout << "轻松拆散指定文件夹\n";
		cout << "输入要拆包的文件夹路径, 输入英文字符:打开设置\n";
		i_input
		getline(cin, sourceDir);
		if(sourceDir == ":" or sourceDir == ":\n")
		{
			while(1)
			{
				char x;
				_color(10);
				cout<<"\n设置\n";
				_color();
				i_input cout<<"0=保存&退出\n";
				i_input cout<<"1=打开右键菜单 PackOut - Remain (不覆盖 | 保留)\n";
				i_input cout<<"2=打开右键菜单 PackOut - AutoDel (覆盖 | 自动删除)\n";
				i_input cout<<"3=关闭所有右键菜单\n\n";
				i_input cin>>x;
				if(x=='0')goto st1;

				char _now[MAX_PATH];
				GetModuleFileNameA(NULL, _now, MAX_PATH);
				string now(_now);
				for(int f=0; f<now.size(); f++)
				{
					if(now.at(f)=='"')
					{
						now.erase(f, 1);
						f--;
						continue;
					}
					if(now.at(f)=='\\')
					{
						now.insert(f, 1, '\\');
						f++;
						continue;
					}
				}
				if(x=='1')
				{
					string regg="";
					regg+=R"(Windows Registry Editor Version 5.00

[HKEY_CLASSES_ROOT\Directory\shell\PackOut]
@="PackOut - Remain"
"Icon"="\")"+now+R"(\""

[HKEY_CLASSES_ROOT\Directory\shell\PackOut\command]
@="\")"+now+R"(\" \"%V\" 1 0")";
					ofstream outFile1("remain.reg");

					if (!outFile1)
					{
						_color(12);
						cerr<<"无法保存文件"<<endl;
						_color();
						return 1;
					}

					outFile1<<regg;
					outFile1.close();

					if (windCMD("regedit /s .\\remain.reg", true, CREATE_NO_WINDOW))
					{
						_color(10);
						cout << "\nSUCCESS!" << endl;
						_color();
					}
					else
					{
						_color(12);
						cout << "\nFAILED! 请用管理员权限运行本程序, 或者到本程序所在目录打开remain.reg" << endl;
						_color();
						system("pause");
						return 1;
					}
					DeleteFileA(".\\reamin.reg");
				}
				if(x=='2')
				{
					string regg="";
					regg+=R"(Windows Registry Editor Version 5.00

[HKEY_CLASSES_ROOT\Directory\shell\PackOut]
@="PackOut - AutoDel"
"Icon"="\")"+now+R"(\""

[HKEY_CLASSES_ROOT\Directory\shell\PackOut\command]
@="\")"+now+R"(\" \"%V\" 2 1")";
					ofstream outFile2("autodel.reg");

					if (!outFile2)
					{
						_color(12);
						cerr<<"无法保存文件"<<endl;
						_color();
						return 1;
					}

					outFile2<<regg;
					outFile2.close();

					if (windCMD("regedit /s .\\autodel.reg", true, CREATE_NO_WINDOW))
					{
						_color(10);
						cout << "\nSUCCESS!" << endl;
						_color();
					}
					else
					{
						_color(12);
						cout << "\nFAILED! 请用管理员权限运行, 或者到本程序所在目录打开autodel.reg" << endl;
						_color();
						system("pause");
						return 1;
					}
					DeleteFileA(".\\autodel.reg");
				}
				if(x=='3')
				{
					string regg="";
					regg+=R"(Windows Registry Editor Version 5.00

[-HKEY_CLASSES_ROOT\Directory\shell\PackOut])";
					ofstream outFile3("del.reg");

					if (!outFile3)
					{
						_color(12);
						cerr<<"无法保存文件"<<endl;
						_color();
						return 1;
					}

					outFile3<<regg;
					outFile3.close();

					if (windCMD("regedit /s .\\del.reg", true, CREATE_NO_WINDOW))
					{
						_color(10);
						cout << "\nSUCCESS!" << endl;
						_color();
					}
					else
					{
						_color(12);
						cout << "\nFAILED! 请用管理员权限运行, 或者到本程序所在目录打开del.reg" << endl;
						_color();
						system("pause");
						return 1;
					}
					DeleteFileA(".\\del.reg");
				}
			}
		}
		// 删除首尾双引号
		if (!sourceDir.empty() && sourceDir.front() == '"')
		{
			sourceDir.erase(0, 1);
		}
		if (!sourceDir.empty() && sourceDir.back() == '"')
		{
			sourceDir.pop_back();
		}
	}

	// 标准化路径
	sourceDir = standardPath(sourceDir);

	// 确保路径格式正确 (去除尾部反斜杠，除非是根目录)
	if (!sourceDir.empty() && sourceDir.back() == '\\' && !isRootDirectory(sourceDir))
	{
		sourceDir.pop_back();
	}

	try
	{
		// 验证源路径是否存在
		if (!directoryExists(sourceDir))
		{
			_color(12);
			cout << "错误: 源目录不存在!" << endl;
			_color();
			return 1;
		}

		// 自动计算目标目录 (父目录)
		targetDir = getParentDirectory(sourceDir);
		cout << "自动设置目标目录为: " << targetDir << endl;

		// 询问覆盖选项（如果未通过命令行设置）
		if (overwriteOption == -1)
		{
			_color(10);
			cout << "\n请选择文件覆盖选项:\n";
			_color();
			i_input cout << "0=测试用, 请勿输入;\n";
			i_input cout << "1=不覆盖已有文件;\n";
			i_input cout << "2=覆盖已有文件;\n\n";
			i_input cin >> overwriteOption;
			cin.ignore();

			if (overwriteOption < 0 || overwriteOption > 2)
			{
				cout << "输入无效, 使用默认值[不覆盖]" << endl;
				overwriteOption = 1;
			}
		}

		// 询问自动删除选项（如果未通过命令行设置）
		if (autoDelOption == -1)
		{
			_color(10);
			cout << "\n请选择文件自动删除选项:\n";
			_color(0);
			i_input cout << "0=保留源文件夹;\n";
			i_input cout << "1=删除源文件夹;\n\n";
			i_input cin >> autoDelOption;
			cin.ignore();

			if (autoDelOption < 0 || autoDelOption > 1)
			{
				cout << "输入无效, 使用默认值[保留源文件夹]" << endl;
				autoDelOption = 0;
			}
		}

		cout<<"\n================\n\n";
		cout<<"本部分功能未完善, 警告可忽略\n";
		// 生成目录备份记录
		string backupFilename = generateFileName("packout_backup");

		string cmd0 = "tree /f \"" + sourceDir + "\" > .\\" + backupFilename + ".txt";

		if (windCMD(cmd0, true, CREATE_NO_WINDOW))
		{
			if (!backupFilename.empty())
			{
				cout << "目录记录已保存到: " << backupFilename << endl;
			}
		}
		else
		{
			_color(14);
			cout << "警告: 无法创建目录记录文件" << endl;
			_color();
		}

		cout<<"\n================\n";

		try
		{
			// 生成复制脚本
			string script = generateXcopyCMD(
			                    sourceDir,
			                    targetDir,
			                    overwriteOption,  // 覆盖选项
			                    autoDelOption  // 移动操作删除源文件
			                );

			// 创建临时脚本文件
			string scriptFile = createTempFilename();
			ofstream out(scriptFile);
			out << script;
			out.close();

			_color(10);
			cout << "\n已创建临时bat: " << scriptFile << endl;
			cout << "开始执行bat" << endl;
			_color();

			// 构建命令行
			string cmd = "cmd.exe /C \"" + scriptFile + "\"";

			cout<<"\n================\n";

			// 执行脚本
			if (windCMD(cmd, true, CREATE_NO_WINDOW))
			{
				_color(10);
				cout << "\nSUCCESS!" << endl;
				_color();
				// 删除临时脚本
				DeleteFileA(scriptFile.c_str());
				_color(10);
				cout << "\n临时bat已删除"<<endl;
				_color();
			}
			else
			{
				_color(12);
				cout << "\nFAILED!" << endl;
				cout << "临时bat保留在: " << scriptFile << endl;
				_color();
				return 1;
			}
		}
		catch (const exception& e)
		{
			_color(12);
			cout << "错误: " << e.what() << endl;
			_color();
			return 1;
		}
	}
	catch (const exception& e)
	{
		_color(12);
		cout << "错误: " << e.what() << endl;
		_color();
		return 1;
	}

	return 0;
}
