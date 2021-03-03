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
	//���ļ��ж�ȡ��������
	static bool Read(const std::string &name, std::string *body) {
		//һ��Ҫע���Զ����Ʒ�ʽ���ļ�
		std::ifstream fs(name, std::ios::binary); //�����ļ���
		if (fs.is_open() == false) {
			std::cout << "open file " << name << " falied\n";
			return false;
		}
		//boost::filesystem::file_size()  ��ȡ�ļ���С
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);//��BODY����ռ�����ļ�����
		fs.read(&(*body)[0], fsize); //��Ϊbody�Ǹ�ָ�룬������Ҫ�Ƚ�����
		if (fs.good() == false) {
			std::cout << "file " << name << " read data failed!\n";
			return false;
		}
		fs.close();
		return true;
	}
	//���ļ���д������
	static bool Write(const std::string &name, const std::string &body) {
		//�����  --ofstreamĬ�ϴ��ļ���ʱ������ԭ������
		//��ǰ�����Ƿ񸲸�д��
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
	bool Insert(const std::string &key, const std::string &val)//����/��������
	{
		_backup_list[key] = val;
		Storage();
		return true;
	}
	bool GetEtag(const std::string &key, std::string *val)//ͨ���ļ������etag��Ϣ
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end()) {
			return false;
		}
		*val = it->second;
		return true;
	}
	bool Storage()//�־û��洢
	{
		//filename etag\r\n
		std::stringstream tmp; //ʵ����һ��string������		
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it) {
			tmp << it->first << " " << it->second << "\r\n";
		}		
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}
	bool InitLoad() //��ʼ������ԭ������
	{
		//�����ݵĳ־û��洢�ļ��м�������
	   //1.����������ļ������ݶ�ȡ����
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false) {
			return false;
		}
		//2.�����ַ�����������\r\n���зָ�
		//boost::split(vector,src,sep,flag)
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.ÿһ�а��տո���зָ� -ǰ��ʱkey��������val
		for (auto i : list) {
			size_t pos = i.find(" ");
			if (pos == std::string::npos) {
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.��key/val��ӵ�_file_list��
			Insert(key, val);
		}
		return true;
	}
private:
	std::string _store_file; //�־û��洢�ļ�����
	std::unordered_map<std::string, std::string> _backup_list; //���ݵ��ļ���Ϣ
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
				std::string pathname = _listen_dir + name; //�ļ�·����
				std::cout << pathname << " is need to backup\n";
				//��ȡ�ļ����ݣ���Ϊ��������
				std::string body;
				FileUtil::Read(pathname, &body);
				//ʵ����client����׼������HTTP�ϴ��ļ�����
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200)) {
					//����ļ��ϴ�����ʧ��
					std::cout << pathname << " backup failed\n";
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag); //���ݳɹ�������/������Ϣ
			
				std::cout << pathname << " backup success\n";

			}
			Sleep(1000);
		}
		return true;
	}
	
	
	bool GetBackupFileList(std::vector<std::string> *list)//��ȡ��Ҫ���ݵ��ļ��б�
	{
		//1.����Ŀ¼��أ���ȡָ��Ŀ¼�������ļ�����
		boost::filesystem::directory_iterator begin(_listen_dir);               
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin) {
			if (boost::filesystem::is_directory(begin->status())) {
				//Ŀ¼����Ҫ����
				//��ǰ�������Ŀ¼���ݣ�����Ŀ¼ֱ������
				continue;
			}
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			std::string cur_etag;
			GetEtag(pathname, &cur_etag);
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);
			if (cur_etag != old_etag) {
				list->push_back(name);//��ǰetag��ԭ��etag��ͬ����Ҫ����
			}
		}

		//2.����ļ���������ǰetag
		//3.��data_manage�б����ԭ��etag���б���
			//1.û���ҵ�ԭ��etag --���ļ���Ҫ����
			//2.�ҵ�ԭ��etag�����ǵ�ǰetag��ԭ��etag����ȣ���Ҫ����
			//3.�ҵ�ԭ��etag�������뵱ǰetag��ȣ�����Ҫ����

		return true;
	}
	static bool GetEtag(const std::string &pathname, std::string *etag)//�����ļ���etag��Ϣ
	{
		//etag:�ļ���С-�ļ����һ���޸�ʵ��  --md5
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;
	}
private:
	std::string _srv_ip;
	uint16_t _srv_port;
	std::string _listen_dir; //��ص�Ŀ¼��Ϣ
	DataManager data_manage;
};