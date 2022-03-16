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
#include <xcb/xcb.h> //xcb_connection_t type, xcb_setup_t type, xcb_get_setup - requires -l xcb option
#include <xcb/xcb_keysyms.h> //xcb_key_symbols_t type, xcb_key_symbols_alloc, xcb_key_symbols_free, xcb_key_symbols_get_keysym - requires -l xcb option - also requires may require - xcb-keysyms option with the use of some variables, functions, etc
#include <X11/Xlib.h> //XStringToKeysym, NoSymbol - requires -l X11 option
#include <xcb/xcb_aux.h> //xcb_aux_get_screen - requires -l xcb-util option

//stands for modular X key bind daemon

std::string sockpath = std::string(getenv("HOME")) + "/.config/mxkbd/mxkbd.socket"; //location of the socket file
bool delete_file = false; //whether to delete non directory files of the path of modepath and pipepath and replace them with a directory
struct sockaddr_un sock;
int sockfd;
int clientfd;
int xcb_fd;
std::string socket_string("");
xcb_connection_t *xcb_conn;
xcb_window_t root_window;
bool chained = false;

bool get_keycode_from_keysym(const unsigned int keysym, unsigned char &outside_keycode) { //might need to replace keysym's unsigned int type with xcb_keysym_t to compile on some architectures
	const xcb_setup_t *setup = xcb_get_setup(xcb_conn);
	if(setup != NULL) {
		xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(xcb_conn); //requires -l xcb-keysyms - otherwise throws undefined reference error
		unsigned char max_kc = setup->max_keycode; //might need to replace max_kc's unsigned char type with xcb_keycode_t to compile on some architectures
		unsigned char kc = setup->min_keycode - 1;
		do {
			kc++;
			for(unsigned char col = 0; col < 4; col++) {
				unsigned int ks = xcb_key_symbols_get_keysym(symbols, kc, col);
				if (ks == keysym) {
					xcb_key_symbols_free(symbols);
					outside_keycode = kc;
					return true;
				}
			}
		} while(kc != max_kc);
		xcb_key_symbols_free(symbols);
	}
	return false;
}

bool get_keycode_from_string(std::string key, unsigned char &outside_keycode) {
	const unsigned int keysym = XStringToKeysym(key.c_str()); //may need to be unsigned long (or x11 lib's KeySym type) as usigned int and unsigned long are not of the same length on all platforms
	if (keysym == NoSymbol) {
		return false;
	}
	return get_keycode_from_keysym(keysym, outside_keycode);
}

bool get_modifier_from_keycode(const unsigned char outside_keycode, unsigned short &mod) {
	bool success = false;
	xcb_get_modifier_mapping_reply_t *mod_mapping_reply = NULL;
	mod_mapping_reply = xcb_get_modifier_mapping_reply(xcb_conn, xcb_get_modifier_mapping(xcb_conn), NULL);
	if(mod_mapping_reply != NULL) {
		unsigned char kpm = mod_mapping_reply->keycodes_per_modifier;
		if(kpm > 0) {
			unsigned char *mod_keycodes = NULL;
			mod_keycodes = xcb_get_modifier_mapping_keycodes(mod_mapping_reply);
			if(mod_keycodes != NULL) {
				unsigned int num_of_mods = xcb_get_modifier_mapping_keycodes_length(mod_mapping_reply) / kpm;
				for(unsigned int i = 0; i < num_of_mods; i++) {
					for(unsigned int j = 0; j < kpm; j++) {
						unsigned char mod_keycode = mod_keycodes[i * kpm + j];
						if(mod_keycode == NoSymbol) {
							continue;
						}
						if(mod_keycode == outside_keycode) {
							mod |= (1 << i);
							success  = true;
						}
					}
				}
			}
		}
	}
	free(mod_mapping_reply);
	return success;
}

bool get_modifier_from_raw_keysym_string(std::string key, unsigned short &mod) {
	unsigned char mod_keycode;
	if(get_keycode_from_string(key, mod_keycode)) {
		return get_modifier_from_keycode(mod_keycode, mod);
	}
	return false;
}

bool get_modifier_from_string(std::string key, unsigned short &mod) {
	if (key.compare("shift") == 0) {
		mod |= XCB_MOD_MASK_SHIFT;
		return true;
	}
	else if (key.compare("control") == 0 || key.compare("ctrl") == 0) {
		mod |= XCB_MOD_MASK_CONTROL;
		return true;
	}
	else if(key.compare("alt") == 0) {
		bool there_is_a_alt = get_modifier_from_raw_keysym_string("Alt_L", mod);
		return get_modifier_from_raw_keysym_string("Alt_R", mod) || there_is_a_alt;
	}
	else if (key.compare("mod1") == 0) {
		mod |= XCB_MOD_MASK_1;
		return true;
	}
	else if (key.compare("mod2") == 0) {
		mod |= XCB_MOD_MASK_2;
		return true;
	}
	else if (key.compare("mod3") == 0) {
		mod |= XCB_MOD_MASK_3;
		return true;
	}
	else if (key.compare("mod4") == 0) {
		mod |= XCB_MOD_MASK_4;
		return true;
	}
	else if (key.compare("mod5") == 0) {
		mod |= XCB_MOD_MASK_5;
		return true;
	}
	else if (key.compare("lock") == 0) {
		mod |= XCB_MOD_MASK_LOCK;
		return true;
	}
	else if (key.compare("any mask") == 0) {
		mod |= XCB_MOD_MASK_ANY;
		return true;
	}
	else {
		unsigned short mod_to_check = 0;
		if(get_modifier_from_raw_keysym_string(key, mod_to_check)) {
			mod |= mod_to_check;
			return true;
		}
		return false;
	}
}

class keybind_grab {
	public:
		unsigned short mods;
		unsigned char keycode;
		bool is_key_release;
		bool setup(std::stringstream &seq) {
			mods = 0;
			is_key_release = false;
			std::string keysym_string = "";
			while(getline(seq, keysym_string, '+')) {
				if(!get_modifier_from_string(keysym_string, mods)) {
					break;
				}
			}
			if(keysym_string.empty()) {
				return false;
			}
			else {
				if(keysym_string[0] == '@') {
					is_key_release = true;
					keysym_string.erase(0, 1);
				}
				return get_keycode_from_string(keysym_string, keycode);
			}
		}
		keybind_grab(std::stringstream &seq, bool &success) {
			success = setup(seq);
		}
		bool operator==(keybind_grab &g) {
			return mods == g.mods && keycode == g.keycode;
		}
		void bind() {
			xcb_grab_key(xcb_conn, false, root_window, mods, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC);
			xcb_flush(xcb_conn); //may or may not need this line
		}
		void unbind() {
			xcb_ungrab_key(xcb_conn, keycode, root_window, mods);
			xcb_flush(xcb_conn); //may or may not need this line
		}
};
std::vector<keybind_grab *> grabbed_keys_n_modifiers;

class keybind_bind {
	public:
		std::stringstream seq;
		std::string description;
		std::string command;
		keybind_grab *grabbed_key_n_modifiers;
		bool setup(std::string bind_seq, std::string bind_command) {
			command = bind_command;
			grabbed_key_n_modifiers = NULL;
			seq.clear();
			seq.str(bind_seq);
			bool success;
			keybind_grab grab_test(seq, success);
			if(!success) {
				return false;
			}
			while(grab_test.setup(seq)) {
			}
			if(!seq.eof()) {
				return false;
			}
			if(grab_test.mods != 0) {
				return false;
			}
			seq.clear();
			seq.seekg(0);
			return true;
		}
		bool setup(std::string bind_seq, std::string bind_command, std::string bind_description) {
			description = bind_description;
			return setup(bind_seq, bind_command);
		}
		keybind_bind(std::string bind_seq, std::string bind_command, bool &success) {
			success = setup(bind_seq, bind_command);
		}
		keybind_bind(std::string bind_seq, std::string bind_command, std::string bind_description, bool &success) {
			success = setup(bind_seq, bind_command, bind_description);
		}
		bool operator==(keybind_bind &o) {
			bool success;
			std::stringstream o_seq(o.seq.str());
			std::stringstream t_seq(seq.str());
			keybind_grab o_grab(o_seq, success);
			keybind_grab t_grab(t_seq, success);
			if(!(t_grab == o_grab)) {
				return false;
			}
			while(o_grab.setup(o_seq) && t_grab.setup(t_seq)) {
				if(!(t_grab == o_grab)) {
					return false;
				}
			}
			return true;
		}
};

bool get_keybind_grab_from_grabbed_keys_n_modifiers(keybind_grab *&comparision_grab, keybind_grab *&grab_searched_for) {
	for(keybind_grab *grab: grabbed_keys_n_modifiers) {
		if((*grab) == (*comparision_grab)) {
			grab_searched_for = grab;
			return true;
		}
	}
	return false;
}

bool add_keybind_grab(std::stringstream &seq, keybind_grab *&grab_searched_for) {
	bool success;
	keybind_grab *grab_from_seq = new keybind_grab(seq, success);
	if((!success) || get_keybind_grab_from_grabbed_keys_n_modifiers(grab_from_seq, grab_searched_for)) {
		delete grab_from_seq;
		return false;
	}
	grab_searched_for = grab_from_seq;
	grabbed_keys_n_modifiers.push_back(grab_from_seq);
	grab_from_seq->bind();
	return true;
}

void delete_all_keybind_grabs() {
	xcb_ungrab_key(xcb_conn, XCB_GRAB_ANY, root_window, XCB_BUTTON_MASK_ANY);
	xcb_flush(xcb_conn); //may or may not need this line
	for(keybind_grab *grab: grabbed_keys_n_modifiers) {
		delete grab;
	}
	grabbed_keys_n_modifiers.clear();
}

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
			if(grabbed_keybind_mode == this) {
				delete_all_keybind_grabs();
			}
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
			bool success;
			keybind_bind keybind_test(seq, std::string("just a test"), success);
			if(!success) {
				return false;
			}
			for(int i = 0; i < (int) keybinds.size(); i++) {
				keybind_bind *keybind = keybinds.at(i);
				if((*keybind) == keybind_test) {
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
			bool is_valid_keybind;
			keybind_searched_for = new keybind_bind(seq, command, is_valid_keybind);
			if(!is_valid_keybind) {
				delete keybind_searched_for;
				return false;
			}
			keybinds.push_back(keybind_searched_for);
			if(grabbed_keybind_mode == this) {
				if(!chained) {
					add_keybind_grab(keybind_searched_for->seq, keybind_searched_for->grabbed_key_n_modifiers);
				}
			}
			return true;
		}
		bool remove_a_keybind(std::string seq) {
			keybind_bind *keybind_bind_to_remove = NULL;
			int l;
			if(get_keybind_from_keybinds(seq, keybind_bind_to_remove, l)) {
				keybinds.erase(keybinds.begin() + l);
				keybind_grab *removed_keybind_grab = keybind_bind_to_remove->grabbed_key_n_modifiers;
				if(removed_keybind_grab == NULL) {
					delete keybind_bind_to_remove;
					return true;
				}
				for(keybind_bind *bind: keybinds) {
					if(removed_keybind_grab == bind->grabbed_key_n_modifiers) {
						delete keybind_bind_to_remove;
						return true;
					}
				}
				removed_keybind_grab->unbind();
				for(int i = 0; i < (int) grabbed_keys_n_modifiers.size(); i++) {
					if(grabbed_keys_n_modifiers.at(i) == removed_keybind_grab) {
						grabbed_keys_n_modifiers.erase(grabbed_keys_n_modifiers.begin() + i);
						break;
					}
				}
				delete removed_keybind_grab;
				delete keybind_bind_to_remove;
				return true;
			}
			else {
				return false;
			}
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

void init_keybind_grabs() {
	if(grabbed_keybind_mode == NULL) {
		return;
	}
	chained = false;
	delete_all_keybind_grabs();
	for(keybind_bind *bind: grabbed_keybind_mode->keybinds) {
		bind->seq.clear();
		bind->seq.seekg(0);
		bind->grabbed_key_n_modifiers = NULL;
		add_keybind_grab(bind->seq, bind->grabbed_key_n_modifiers);
	}
}

void update_grabs(unsigned char kc, unsigned short mods) {
	keybind_grab *grab_searched_for;
	for(keybind_grab *grab: grabbed_keys_n_modifiers) {
		if(grab->keycode == kc && grab->mods == mods) {
			grab_searched_for = grab;
			break;
		}
	}
	chained = true;
	std::vector<keybind_bind *> binds;
	for(keybind_bind *bind: grabbed_keybind_mode->keybinds) {
		if(bind->grabbed_key_n_modifiers == grab_searched_for) {
			if(bind->seq.eof()) {
				system((bind->command + " &").c_str());
				init_keybind_grabs();
				return;
			}
			binds.push_back(bind);
		}
		else {
			bind->seq.clear();
			bind->seq.seekg(0);
		}
		bind->grabbed_key_n_modifiers == NULL;
	}
	delete_all_keybind_grabs();
	for(keybind_bind *bind: binds) {
		add_keybind_grab(bind->seq, bind->grabbed_key_n_modifiers);
	}
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
					build_up_socket_string(std::string("keybind already exists or was invalid in syntax"));
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
					init_keybind_grabs();
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
	keybind_mode *remove_operation_mode = selected_keybind_mode;
	std::string prop_of_mode_or_keybind = "";
	int l = -1;
	if(is_option_parse_string(p, std::string("-g"))) {
		remove_operation_mode  = grabbed_keybind_mode;
		goto set_prop_of_mode_or_keybind;
	}
	else if(is_option_parse_string(p, std::string("-s"))) {
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
		remove_operation_mode = selected_keybind_mode;
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
			if(remove_operation_mode->remove_a_keybind(prop_of_mode_or_keybind)) {
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

void setup_xcb() {
	int conn_screen;
	xcb_conn = xcb_connect(NULL, &conn_screen);
	xcb_screen_t *root_screen = xcb_aux_get_screen(xcb_conn, conn_screen);
	root_window = root_screen->root;
	xcb_fd = xcb_get_file_descriptor(xcb_conn);
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
	setup_xcb();
	struct pollfd socket_poll[2] = {
		{sockfd, POLLIN, 0},
		{xcb_fd, POLLIN, 0}
	};
	while(poll(socket_poll, 2, -1) > 0) {
		if((socket_poll[0].revents & POLLIN) == POLLIN) {
			clientfd = accept(sockfd, NULL, NULL);
			std::stringstream client_message = read_from_socket();
			socket_string = "";
			add_operation(client_message) || remove_operation(client_message) || set_operation(client_message) || list_operation(client_message);
			write_to_socket(socket_string);
			close(clientfd);
		}
		else if((socket_poll[1].revents & POLLIN) == POLLIN) {
			xcb_generic_event_t *event;
			while ((event = xcb_poll_for_event(xcb_conn))) {
				switch (event->response_type & ~0x80) {
					case XCB_KEY_PRESS: {
						xcb_key_press_event_t *button_event = (xcb_key_press_event_t *) event;
						update_grabs(button_event->detail, button_event->state);
						xcb_allow_events(xcb_conn, XCB_ALLOW_SYNC_KEYBOARD, XCB_CURRENT_TIME);
						break;
					}
					default: {
						break;
					}
				}
			}
			free(event);
			xcb_flush(xcb_conn); //may or may not need this line
		}
	}
	return 0;
}
