#include"cloud_client.hpp"

#define STORE_FILE "./list.backup"
#define LISTEN_DIR "./backup/"
#define SERVER_IP "192.168.231.128"
#define SERVER_PORT 9000

int main()
{
	
	if (boost::filesystem::exists(STORE_FILE) == false) {
		boost::filesystem::create_directories(STORE_FILE);
	}
	
	if (boost::filesystem::exists(LISTEN_DIR) == false) {
		boost::filesystem::create_directory(LISTEN_DIR);
	}
	CloudClient client(LISTEN_DIR, STORE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();

	return 0;
}

