#include <getopt.h> //getopt_long, required_argument
#include <string> //std::string
#include <sys/stat.h> //stat function, stat struct, S_ISDIR macro
#include <stdio.h> //remove file function
#include <iomanip> //std::quoted
#include <sstream> //stringstream
#include <sys/poll.h> //poll function, pollfd struct
#include <sys/socket.h> //socket, SOCK_STREAM, AF_UNIX
#include <sys/un.h> //struct sockaddr_un
#include <fcntl.h> //O_CREAT, O_EXCL
#include <unistd.h> //unlink
#include <cstdlib> //std::atexit
#include <csignal> //signal
#include <poll.h> //poll, POLLIN, pollfd
#include <vector>

//stands for modular X key bind daemon

std::string sockpath = std::string(getenv("HOME")) + "/.config/mxkbd/mxkbd.socket"; //location of the socket file
bool delete_file = false; //whether to delete non directory files of the path of modepath and pipepath and replace them with a directory
struct sockaddr_un sock;
int sockfd;
int clientfd;
std::string socket_string("");

class keybind_grab {
	public:
		unsigned short modifiers;
		unsigned char keycode;
};
std::vector<keybind_grab> grabbed_keys_n_modifiers;

class keybind_bind {
	public:
		std::stringstream seq;
		std::string description;
		std::string command;
		keybind_grab *grabbed_key_n_modifiers;
		keybind_bind(std::string bind_seq, std::string bind_command) {
			seq.clear();
			seq.str(bind_seq);
			command = bind_command;
			grabbed_key_n_modifiers = NULL;
		}
		keybind_bind(std::string bind_seq, std::string bind_command, std::string bind_description) {
			seq.clear();
			seq.str(bind_seq);
			command = bind_command;
			description = bind_description;
			grabbed_key_n_modifiers = NULL;
		}
};

class keybind_mode;
keybind_mode *default_keybind_mode = NULL; //mode to set the 2 below to if the mode being deleted is one of them
keybind_mode *grabbed_keybind_mode = NULL; //mode to grab keys off of it's keybinds member
keybind_mode *selected_keybind_mode = NULL; //mode to and from which add and remove keybinds 
std::vector<keybind_mode*> modes;
class keybind_mode {
	public:
		std::string name;
		std::string description;
		std::vector<keybind_bind*> keybinds;
		keybind_mode(std::string mode_name) {
			name = mode_name;
		}
		~keybind_mode() {
			for(keybind_bind *bind: keybinds) {
				delete bind;
			}
			if(default_keybind_mode == this) {
				default_keybind_mode = NULL;
			}
			if(grabbed_keybind_mode == this) {
				grabbed_keybind_mode = default_keybind_mode;
			}
			if(selected_keybind_mode == this) {
				selected_keybind_mode = grabbed_keybind_mode;
			}
		}
		bool get_keybind_from_keybinds(std::string seq, keybind_bind *&keybind_searched_for, int &l) {
			for(int i = 0; i < (int) keybinds.size(); i++) {
				keybind_bind *keybind = keybinds.at(i);
				if(keybind->seq.str().compare(seq) == 0) {
					keybind_searched_for = keybind;
					l = i;
					return true;
				}
			}
			return false;
		}
		bool get_keybind_from_keybinds(std::string seq, keybind_bind *&keybind_searched_for) {
			int l = -1;
			return get_keybind_from_keybinds(seq, keybind_searched_for, l);
		}
		bool add_to_keybinds(std::string seq, std::string command, keybind_bind *&keybind_searched_for) {
			if(get_keybind_from_keybinds(seq, keybind_searched_for)) {
				return false;
			}
			keybind_searched_for = new keybind_bind(seq, command);
			keybinds.push_back(keybind_searched_for);
			return true;
		}
};

bool get_mode_from_modes(std::string mode_name, keybind_mode *&mode_searched_for, int &l) {
	for(int i = 0; i < modes.size(); i++) {
		keybind_mode *mode = modes.at(i);
		if(mode->name.compare(mode_name) == 0) {
			mode_searched_for = mode;
			l = i;
			return true;
		}
	}
	return false;
}

bool get_mode_from_modes(std::string mode_name, keybind_mode *&mode_searched_for) {
	int l = -1;
	return get_mode_from_modes(mode_name, mode_searched_for, l);
}

bool add_to_modes(std::string mode_name, keybind_mode *&mode_searched_for) {
	if(get_mode_from_modes(mode_name, mode_searched_for)) {
		return false;
	}
	mode_searched_for = new keybind_mode(mode_name);
	modes.push_back(mode_searched_for);
	return true;
}

int isdir(mode_t mode) {
	return S_ISDIR(mode);
}

int issock(mode_t mode) {
	return S_ISSOCK(mode);
}

int isfile(mode_t mode) {
	return S_ISREG(mode);
}

void create_dir(std::string &dir) {
	std::stringstream mkdir("");
	mkdir << "mkdir -p " << std::quoted(dir);
	if(system(mkdir.str().c_str()) != 0) {
		exit(1);
	}
}

bool file_of_type_setup(std::string &file, int (*is_file_of_type)(mode_t mode), void (*create_file_of_type)(std::string &filepath)) { //return true if file already exists, otherwise return false
	struct stat sb;
	if(stat(file.c_str(), &sb) == 0) { //check if file exists
		if(!is_file_of_type(sb.st_mode)) { //check if file isn't of type
			if(!delete_file || remove(file.c_str()) != 0) { //delete file that isn't of type if delete_file is true otherwise exit
				exit(1); //exit with failure if file can't be deleted
			}
			create_file_of_type(file); //create file of type after if it was deleted as a result of not being of type
		}
		else {
			return true;
		}
	}
	else {
		create_file_of_type(file); //create file of type as it doesn't exist
	}
	return false;
}

void create_sock(std::string &sockfile) {
	strcpy(sock.sun_path, sockfile.c_str());
	if(bind(sockfd, (struct sockaddr *) &sock, sizeof(struct sockaddr_un)) == -1) {
		exit(-1);
	}
	if (listen(sockfd, 4096) == -1) {
		exit(-1);
	}
}

void directory_setup(std::string &directory) { //create directory if it doesn't exists or delete_file is true and directory parameter isn't a directory (after deleting it that is)
	file_of_type_setup(directory, &isdir, &create_dir);
}

bool non_directory_setup(std::string &non_directory, int (*is_file_of_non_directory_type)(mode_t mode), void (*create_file_of_non_directory_type)(std::string &filepath)) {
	int parentdirend = non_directory.rfind('/');
	if(parentdirend != std::string::npos) {
		std::string parentdir = non_directory.substr(0, parentdirend);
		directory_setup(parentdir);
	}
	return file_of_type_setup(non_directory, is_file_of_non_directory_type, create_file_of_non_directory_type);
}

void socket_setup(std::string &socketfile) {
	if(non_directory_setup(socketfile, &issock, &create_sock)) {
		if(unlink(socketfile.c_str()) == -1) {
			exit(-1);
		}
		create_sock(socketfile);
	}
}

void get_rid_of_socket_file() {
	unlink(sockpath.c_str());
}

void just_exit_normally(int na) {
	exit(0);
}

void exit_if_mxkbd_already_running() {
	std::stringstream pidof(""); //not initializing to "pidof -o" here because next line will disregard it. Maybe have to use multiple << all in one line as each subsequent line with << stringstream assignment might disregard the assignment of previous lines?
	pidof << "pidof -o " << (int) getpid() << " mxkbd"; //check if there exists a pid of mxkbd other than (-o or omit) this program's pid (getpid())
	if(system(pidof.str().c_str()) == 0) { //if the pidof command is successful (returns 0 and prints the pid of the other running instance of mxkbd) then exit with error
		exit(-1);
	}
}

std::stringstream read_from_socket() {
	char dataPtr[32768] = {};
	std::string contents = "";
	if(read(clientfd, dataPtr, 32767) < 0) {
		exit(-1);
	}
	contents += dataPtr;
	return std::stringstream(contents);
}

bool parse_client_message(std::stringstream &p, std::string &r) {
	int length_to_read = 0;
	p >> length_to_read;
	if(p.eof()) {
		return false;
	}
	char *buf = new char [length_to_read + 1]();
	p.ignore();
	p.read(buf, length_to_read);
	if(p.eof()) {
		delete[] buf;
		return false;
	}
	r = buf;
	delete[] buf;
	return true;
}

bool is_option_parse_string(std::stringstream &p, std::string o) {
	std::string next_option = "";
	std::streampos current_pos = p.tellg();
	if(!parse_client_message(p, next_option)) {
		return false;
	}
	if(next_option.compare(o) == 0) {
		return true;
	}
	p.seekg(current_pos);
	return false;
}

void write_to_socket(std::string write_string) {
	if(write(clientfd, write_string.c_str(), write_string.length()) < 0) {
		exit(-1);
	}
}

void build_up_socket_string(std::string string_to_add) {
	socket_string += std::to_string(string_to_add.length()) + " " + string_to_add;
}

bool add_operation(std::stringstream &p) {
	if(!(is_option_parse_string(p, std::string("a")) || is_option_parse_string(p, std::string("add")))) {
		return false;
	}
	if(is_option_parse_string(p, std::string("m")) || is_option_parse_string(p, std::string("mode"))) {
		std::string new_mode_prop = "";
		if(parse_client_message(p, new_mode_prop)) {
			keybind_mode *new_mode = NULL;
			if(add_to_modes(new_mode_prop, new_mode)) {
				if(parse_client_message(p, new_mode_prop)) {
					new_mode->description = new_mode_prop;
				}
				build_up_socket_string(std::string("ok"));
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("mode already exists"));
			}
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify mode"));
		}
	}
	else if(is_option_parse_string(p, std::string("k")) || is_option_parse_string(p, std::string("keybind"))) {
		keybind_mode *mode_of_keybind = selected_keybind_mode;
		std::string prop_of_mode_or_keybind = "";
		if(is_option_parse_string(p, std::string("-g"))) {
			mode_of_keybind = grabbed_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-s"))) {
			mode_of_keybind = selected_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-d"))) {
			mode_of_keybind = default_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-m"))) {
			if(parse_client_message(p, prop_of_mode_or_keybind)) {
				if(!get_mode_from_modes(prop_of_mode_or_keybind, mode_of_keybind)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
				}
			}
		}
		if(mode_of_keybind == NULL) {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("mode is NULL"));
			return true;
		}
		if(parse_client_message(p, prop_of_mode_or_keybind)) {
			std::string keybind_command = "";
			if(parse_client_message(p, keybind_command)) {
				keybind_bind *new_keybind_bind = NULL;
				if(mode_of_keybind->add_to_keybinds(prop_of_mode_or_keybind, keybind_command, new_keybind_bind)) {
					if(parse_client_message(p, prop_of_mode_or_keybind)) {
						new_keybind_bind->description = prop_of_mode_or_keybind;
					}
					build_up_socket_string(std::string("ok"));
				}
				else {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("keybind already exists"));
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify keybind command"));
			}
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify keybind key sequence"));
		}
	}
	else {
		build_up_socket_string(std::string("err"));
		build_up_socket_string(std::string("did not specify option"));
	}
	return true;
}

bool set_operation(std::stringstream &p) {
	if(!(is_option_parse_string(p, std::string("s")) || is_option_parse_string(p, std::string("set")))) {
		return false;
	}
	keybind_mode *set_operation_mode = selected_keybind_mode;
	std::string prop_of_mode_or_keybind = "";
	if(is_option_parse_string(p, std::string("-g"))) {
		set_operation_mode = grabbed_keybind_mode;
		goto set_mode_member;
	}
	else if(is_option_parse_string(p, std::string("-s"))) {
		set_operation_mode = selected_keybind_mode;
		goto set_mode_member;
	}
	else if(is_option_parse_string(p, std::string("-d"))) {
		set_operation_mode = default_keybind_mode;
		goto set_mode_member;
	}
	else if(is_option_parse_string(p, std::string("m")) || is_option_parse_string(p, std::string("mode"))) {
		if(parse_client_message(p, prop_of_mode_or_keybind)) {
			if(!get_mode_from_modes(prop_of_mode_or_keybind, set_operation_mode)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
			}
			set_mode_member:
				if(set_operation_mode == NULL) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode is NULL"));
					return true;
				}
				if(is_option_parse_string(p, std::string("-g"))) {
					grabbed_keybind_mode = set_operation_mode;
					build_up_socket_string(std::string("ok"));
				}
				else if(is_option_parse_string(p, std::string("-s"))) {
					selected_keybind_mode = set_operation_mode;
					build_up_socket_string(std::string("ok"));
				}
				else if(is_option_parse_string(p, std::string("-d"))) {
					default_keybind_mode = set_operation_mode;
					build_up_socket_string(std::string("ok"));
				}
				else if(is_option_parse_string(p, std::string("n")) || is_option_parse_string(p, std::string("name"))) {
					if(parse_client_message(p, prop_of_mode_or_keybind)) {
						set_operation_mode->name = prop_of_mode_or_keybind;
						build_up_socket_string(std::string("ok"));
					}
					else {
						build_up_socket_string(std::string("err"));
						build_up_socket_string(std::string("did not specify new mode name"));
					}
				}
				else if(is_option_parse_string(p, std::string("d")) || is_option_parse_string(p, std::string("description"))) {
					if(parse_client_message(p, prop_of_mode_or_keybind)) {
						set_operation_mode->description = prop_of_mode_or_keybind;
						build_up_socket_string(std::string("ok"));
					}
					else {
						build_up_socket_string(std::string("err"));
						build_up_socket_string(std::string("did not specify mode description"));
					}
				}
				else {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("did not specify what property of mode to set nor the 3 special keybind modes"));
				}
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify mode"));
		}
	}
	else if(is_option_parse_string(p, std::string("k")) || is_option_parse_string(p, std::string("keybind"))) {
		if(is_option_parse_string(p, std::string("-g"))) {
			set_operation_mode  = grabbed_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-s"))) {
			set_operation_mode  = selected_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-d"))) {
			set_operation_mode  = default_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-m"))) {
			if(parse_client_message(p, prop_of_mode_or_keybind)) {
				if(!get_mode_from_modes(prop_of_mode_or_keybind, set_operation_mode)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify mode"));
				return true;
			}
		}
		if(set_operation_mode == NULL) {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("mode is NULL"));
			return true;
		}
		if(parse_client_message(p, prop_of_mode_or_keybind)) {
			keybind_bind *new_keybind_bind = NULL;
			if(selected_keybind_mode->get_keybind_from_keybinds(prop_of_mode_or_keybind, new_keybind_bind)) {
				if(is_option_parse_string(p, std::string("s")) || is_option_parse_string(p, std::string("seq"))) {
					if(parse_client_message(p, prop_of_mode_or_keybind)) {
						new_keybind_bind->seq.clear();
						new_keybind_bind->seq.str(prop_of_mode_or_keybind);
						build_up_socket_string(std::string("ok"));
					}
					else {
						build_up_socket_string(std::string("err"));
						build_up_socket_string(std::string("did not specify keybind key sequence"));
					}
				}
				else if(is_option_parse_string(p, std::string("d")) || is_option_parse_string(p, std::string("description"))) {
					if(parse_client_message(p, prop_of_mode_or_keybind)) {
						new_keybind_bind->description = prop_of_mode_or_keybind;
						build_up_socket_string(std::string("ok"));
					}
					else {
						build_up_socket_string(std::string("err"));
						build_up_socket_string(std::string("did not specify keybind description"));
					}
				}
				else if(is_option_parse_string(p, std::string("c")) || is_option_parse_string(p, std::string("command"))) {
					if(parse_client_message(p, prop_of_mode_or_keybind)) {
						new_keybind_bind->command = prop_of_mode_or_keybind;
						build_up_socket_string(std::string("ok"));
					}
					else {
						build_up_socket_string(std::string("err"));
						build_up_socket_string(std::string("did not specify keybind command"));
					}
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("keybind key sequence does not exist"));
			}
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify keybind key sequence"));
		}
	}
	else {
		build_up_socket_string(std::string("err"));
		build_up_socket_string(std::string("did not specify option"));
	}
	return true;
}

bool remove_operation(std::stringstream &p) {
	if(!(is_option_parse_string(p, std::string("r")) || is_option_parse_string(p, std::string("remove")))) {
		return false;
	}
	keybind_mode *remove_operation_mode = NULL;
	std::string prop_of_mode_or_keybind = "";
	int l = -1;
	if(is_option_parse_string(p, std::string("-g"))) {
		remove_operation_mode  = grabbed_keybind_mode;
		goto set_prop_of_mode_or_keybind;
	}
	else if(is_option_parse_string(p, std::string("-s"))) {
		remove_operation_mode  = selected_keybind_mode;
		goto set_prop_of_mode_or_keybind;
	}
	else if(is_option_parse_string(p, std::string("-d"))) {
		remove_operation_mode  = default_keybind_mode;
		set_prop_of_mode_or_keybind:
			prop_of_mode_or_keybind = remove_operation_mode->name;
			goto remove_mode_member;
	}
	else if(is_option_parse_string(p, std::string("m")) || is_option_parse_string(p, std::string("mode"))) {
		if(parse_client_message(p, prop_of_mode_or_keybind)) {
			remove_mode_member:
				if(!get_mode_from_modes(prop_of_mode_or_keybind, remove_operation_mode, l)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
				}
				if(remove_operation_mode == NULL) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode is NULL"));
				}
				else {
					modes.erase(modes.begin() + l);
					delete remove_operation_mode;
					build_up_socket_string(std::string("ok"));
				}
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify mode"));
		}
	}
	else if(is_option_parse_string(p, std::string("k")) || is_option_parse_string(p, std::string("keybind"))) {
		if(is_option_parse_string(p, std::string("-g"))) {
			remove_operation_mode  = grabbed_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-s"))) {
			remove_operation_mode  = selected_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-d"))) {
			remove_operation_mode  = default_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-m"))) {
			if(parse_client_message(p, prop_of_mode_or_keybind)) {
				if(!get_mode_from_modes(prop_of_mode_or_keybind, remove_operation_mode)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify mode"));
				return true;
			}
		}
		if(remove_operation_mode == NULL) {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("mode is NULL"));
			return true;
		}
		if(parse_client_message(p, prop_of_mode_or_keybind)) {
			keybind_bind *keybind_bind_to_remove = NULL;
			if(remove_operation_mode->get_keybind_from_keybinds(prop_of_mode_or_keybind, keybind_bind_to_remove, l)) {
				remove_operation_mode->keybinds.erase(remove_operation_mode->keybinds.begin() + l);
				delete keybind_bind_to_remove;
				build_up_socket_string(std::string("ok"));
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("keybind key sequence does not exist"));
			}
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify keybind key sequence"));
		}
	}
	else {
		build_up_socket_string(std::string("err"));
		build_up_socket_string(std::string("did not specify option"));
	}
	return true;
}

bool list_operation(std::stringstream &p) {
	if(!(is_option_parse_string(p, std::string("l")) || is_option_parse_string(p, std::string("list")))) {
		return false;
	}
	keybind_mode *list_operation_mode = selected_keybind_mode;
	std::string prop_of_mode_or_keybind = "";
	if(is_option_parse_string(p, std::string("k")) || is_option_parse_string(p, std::string("keybind"))) {
		if(is_option_parse_string(p, std::string("-g"))) {
			list_operation_mode = grabbed_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-s"))) {
			list_operation_mode = selected_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-d"))) {
			list_operation_mode = default_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-m"))) {
			if(parse_client_message(p, prop_of_mode_or_keybind)) {
				if(!get_mode_from_modes(prop_of_mode_or_keybind, list_operation_mode)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify mode"));
				return true;
			}
		}
		if(list_operation_mode == NULL) {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("mode is NULL"));
			return true;
		}
		if(is_option_parse_string(p, std::string("-a"))) {
			if(is_option_parse_string(p, std::string("d")) || is_option_parse_string(p, std::string("description"))) {
				build_up_socket_string(std::string("ok"));
				for(keybind_bind *bind: list_operation_mode->keybinds) {
					build_up_socket_string(bind->description);
				}
			}
			else if(is_option_parse_string(p, std::string("c")) || is_option_parse_string(p, std::string("command"))) {
				build_up_socket_string(std::string("ok"));
				for(keybind_bind *bind: list_operation_mode->keybinds) {
					build_up_socket_string(bind->command);
				}
			}
			else if(!parse_client_message(p, prop_of_mode_or_keybind)) {
				build_up_socket_string(std::string("ok"));
				for(keybind_bind *bind: list_operation_mode->keybinds) {
					build_up_socket_string(bind->seq.str());
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify correct option"));
			}
			return true;
		}
		else {
			if(parse_client_message(p, prop_of_mode_or_keybind)) {
				keybind_bind *new_keybind_bind = NULL;
				if(list_operation_mode->get_keybind_from_keybinds(prop_of_mode_or_keybind, new_keybind_bind)) {
					if(is_option_parse_string(p, std::string("d")) || is_option_parse_string(p, std::string("description"))) {
						build_up_socket_string(std::string("ok"));
						build_up_socket_string(new_keybind_bind->description);
					}
					else if(is_option_parse_string(p, std::string("c")) || is_option_parse_string(p, std::string("command"))) {
						build_up_socket_string(std::string("ok"));
						build_up_socket_string(new_keybind_bind->command);
					}
					else if(!parse_client_message(p, prop_of_mode_or_keybind)) {
						build_up_socket_string(std::string("ok"));
						build_up_socket_string(new_keybind_bind->seq.str());
					}
					else {
						build_up_socket_string(std::string("err"));
						build_up_socket_string(std::string("did not specify correct option"));
					}
				}
			}
		}
	}
	else {
		if(is_option_parse_string(p, std::string("-g"))) {
			list_operation_mode = grabbed_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-s"))) {
			list_operation_mode = selected_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("-d"))) {
			list_operation_mode = default_keybind_mode;
		}
		else if(is_option_parse_string(p, std::string("m")) || is_option_parse_string(p, std::string("mode"))) {
			if(parse_client_message(p, prop_of_mode_or_keybind)) {
				if(!get_mode_from_modes(prop_of_mode_or_keybind, list_operation_mode)) {
					build_up_socket_string(std::string("err"));
					build_up_socket_string(std::string("mode does not exist"));
					return true;
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify mode"));
				return true;
			}
		}
		else if(is_option_parse_string(p, std::string("-a"))) {
			if(is_option_parse_string(p, std::string("d")) || is_option_parse_string(p, std::string("description"))) {
				build_up_socket_string(std::string("ok"));
				for(keybind_mode *mode: modes) {
					build_up_socket_string(mode->description);
				}
			}
			else if(!parse_client_message(p, prop_of_mode_or_keybind)) {
				build_up_socket_string(std::string("ok"));
				for(keybind_mode *mode: modes) {
					build_up_socket_string(mode->name);
				}
			}
			else {
				build_up_socket_string(std::string("err"));
				build_up_socket_string(std::string("did not specify correct option"));
			}
			return true;
		}
		if(list_operation_mode == NULL) {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("mode is NULL"));
			return true;
		}
		if(is_option_parse_string(p, std::string("d")) || is_option_parse_string(p, std::string("description"))) {
				build_up_socket_string(std::string("ok"));
				build_up_socket_string(list_operation_mode->description);
		}
		else if(!parse_client_message(p, prop_of_mode_or_keybind)) {
				build_up_socket_string(std::string("ok"));
				build_up_socket_string(list_operation_mode->name);
		}
		else {
			build_up_socket_string(std::string("err"));
			build_up_socket_string(std::string("did not specify option or keybind property"));
		}
	}
	return true;
}

int main(int argc, char *argv[]) {
	exit_if_mxkbd_already_running();
	{
		int opt;
		int opt_pos; //position within long_opts
		const struct option long_opts[] = {
			{"socket", required_argument, NULL, 'S'},
			{"default", required_argument, NULL, 'd'},
			{"grabbed", required_argument, NULL, 'g'},
			{"select", required_argument, NULL, 's'},
			{"deletefile", no_argument, NULL, 'D'},
			{NULL, 0, NULL, 0} //otherwise segmentation fault will occur when calling with invalid long option
		};
		/*
	    	options:
		    	S, socket:  set the location of the socket file
		    	g, grabbed: set mode to execute keybinds off of
		    	s, select: set mode to add keybinds to
		    	d, deletefile: if set and socket of long_opts above exists and is not a socket file then delete the file and recreate it as a socket - otherwise exit
	    */
		while((opt = getopt_long(argc, argv, ":S:d:g:s:D", long_opts, &opt_pos)) != -1) {
			switch(opt) {
				case 'S':
					sockpath = optarg;
					break;
				case 'd':
					add_to_modes(std::string(optarg), default_keybind_mode); //mode to switch grabbed_mode and select_mode to if they are deleted and the other of the 2 was identical - not meant to be deleted until the end of the program, although deleting the default_mode and then both the grabbed_mode and select_mode could be used to disable mxkbd until mxkbd client sends message to it to redifine grabbed_mode
					break;
				case 'g':
					add_to_modes(std::string(optarg), grabbed_keybind_mode); //mode to execute keybinds off of
					break;
				case 's':
					add_to_modes(std::string(optarg), selected_keybind_mode); //mode to add keybinds to
					break;
				case 'D':
					delete_file = true;
					break;
				case ':':
				case '?':
				default:
					break;
			}
		}
	}
	sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if(sockfd == -1) {
		exit(-1);
	}
	sock.sun_family = AF_UNIX;
	socket_setup(sockpath);
	std::atexit(get_rid_of_socket_file);
	signal(SIGINT, just_exit_normally);
	signal(SIGHUP, just_exit_normally);
	signal(SIGTERM, just_exit_normally);
	struct pollfd socket_poll[1] = {{sockfd, POLLIN, 0}};
	while(poll(socket_poll, 1, -1) > 0) {
		clientfd = accept(sockfd, NULL, NULL);
		std::stringstream client_message = read_from_socket();
		socket_string = "";
		add_operation(client_message) || remove_operation(client_message) || set_operation(client_message) || list_operation(client_message);
		write_to_socket(socket_string);
		close(clientfd);
	}
	return 0;
}
