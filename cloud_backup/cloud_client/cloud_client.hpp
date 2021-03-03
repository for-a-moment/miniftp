#pragma once
#include<iostream>
#include<sstream>
#include<fstream>
#include<vector>
#include<string>
#include<unordered_map>
#include<boost/filesystem.hpp>
#include<boost/algorithm/string.hpp>
#include"httplib.h"



class FileUtil
{
public:
	//从文件中读取所有内容
	static bool Read(const std::string &name, std::string *body) {
		//一定要注意以二进制方式打开文件
		std::ifstream fs(name, std::ios::binary); //输入文件流
		if (fs.is_open() == false) {
			std::cout << "open file " << name << " falied\n";
			return false;
		}
		//boost::filesystem::file_size()  获取文件大小
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);//给BODY申请空间接收文件数据
		fs.read(&(*body)[0], fsize); //因为body是个指针，所以需要先解引用
		if (fs.good() == false) {
			std::cout << "file " << name << " read data failed!\n";
			return false;
		}
		fs.close();
		return true;
	}
	//向文件中写入数据
	static bool Write(const std::string &name, const std::string &body) {
		//输出流  --ofstream默认打开文件的时候会清空原有内容
		//当前策略是否覆盖写入
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false) {
			std::cout << "open file " << name << " falied\n";
			return false;
		}
		ofs.write(&body[0], body.size());

		if (ofs.good() == false) {
			std::cout << "file " << name << " write data failed!\n";
			return false;
		}
		ofs.close();
		return true;
	}
};



class DataManager
{
public:
	DataManager(const std::string &filename):_store_file(filename){}
	bool Insert(const std::string &key, const std::string &val)//插入/更新数据
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}
	bool GetEtag(const std::string &key, std::string *val)//通过文件名获得etag信息
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end()) {
			return false;
		}
		*val = it->second;
		return true;
	}
	bool Storage()//持久化存储
	{
		//filename etag\r\n
		std::stringstream tmp; //实例化一个string流对象		
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it) {
			tmp << it->first << " " << it->second << "\r\n";
		}		
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}
	bool InitLoad() //初始化加载原有数据
	{
		//从数据的持久化存储文件中加载数据
	   //1.将这个备份文件的数据读取出来
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false) {
			return false;
		}
		//2.进行字符串处理，按照\r\n进行分割
		//boost::split(vector,src,sep,flag)
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.每一行按照空格进行分割 -前面时key，后面是val
		for (auto i : list) {
			size_t pos = i.find(" ");
			if (pos == std::string::npos) {
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.将key/val添加到_file_list中
			Insert(key, val);
		}
		return true;
	}
private:
	std::string _store_file; //持久化存储文件名称
	std::unordered_map<std::string, std::string> _backup_list; //备份的文件信息
};




class CloudClient
{
public:
	CloudClient(const std::string &filename, const std::string &store_file,
		const std::string &srv_ip, uint16_t srv_port):
		_listen_dir(filename),data_manage(store_file),_srv_ip(srv_ip),_srv_port(srv_port){}
	bool Start() {
		data_manage.InitLoad();
		while (1) {
			std::vector<std::string> list;
			GetBackupFileList(&list);
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name; //文件路径名
				std::cout << pathname << " is need to backup\n";
				//读取文件数据，作为请求正文
				std::string body;
				FileUtil::Read(pathname, &body);
				//实例化client对象准备发起HTTP上传文件请求
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200)) {
					//这个文件上传备份失败
					std::cout << pathname << " backup failed\n";
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag); //备份成功，插入/更新信息
			
				std::cout << pathname << " backup success\n";

			}
			Sleep(1000);
		}
		return true;
	}
	
	
	bool GetBackupFileList(std::vector<std::string> *list)//获取需要备份的文件列表
	{
		//1.进行目录监控，获取指定目录下所有文件名称
		boost::filesystem::directory_iterator begin(_listen_dir);               
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin) {
			if (boost::filesystem::is_directory(begin->status())) {
				//目录不需要备份
				//当前不做多层目录备份，遇到目录直接跳过
				continue;
			}
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			std::string cur_etag;
			GetEtag(pathname, &cur_etag);
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);
			if (cur_etag != old_etag) {
				list->push_back(name);//当前etag与原有etag不同则需要备份
			}
		}

		//2.逐个文件计算自身当前etag
		//3.与data_manage中保存的原有etag进行备份
			//1.没有找到原有etag --新文件需要备份
			//2.找到原有etag，但是当前etag和原有etag不相等，需要备份
			//3.找到原有etag，并且与当前etag相等，则不需要备份

		return true;
	}
	static bool GetEtag(const std::string &pathname, std::string *etag)//计算文件的etag信息
	{
		//etag:文件大小-文件最后一次修改实践  --md5
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;
	}
private:
	std::string _srv_ip;
	uint16_t _srv_port;
	std::string _listen_dir; //监控的目录信息
	DataManager data_manage;
};