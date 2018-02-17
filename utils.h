#ifndef HW2_UTILS
#define HW2_UTILS
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <set>
#include <fstream>
#include <iterator>

// Escape a character or set of characters in a string with backslash
void escape_str(std::string& str, const std::set<char>& esc){
	std::ostringstream ss;
	for(char c : str){
		if(esc.count(c) || c == '\\') ss << '\\';
		ss << c;
	}
	str = ss.str();
}

// Remove all unescaped backslashes from a string
void unescape_str(std::string& str){
	std::ostringstream ss;
	bool no_esc = true;
	for(char c : str){
		if(c == '\\' && no_esc) no_esc = false;
		else no_esc = true, ss << c;
	}
	str = ss.str();
}

// Check if the character at position i in the string is escaped
bool is_escaped(const std::string& host, int i){
	if(i == 0 || host[i-1] != '\\') return false;
	else return !is_escaped(host, i-1);
}

// Split a string based on a deliminator and store the results in a referenced vector
// Added feature of not splitting on character if it is escaped
void split_str(std::vector<std::string>& res, std::string str, char delim, bool escape=true){
	int i = str.find(delim);
	if(escape){
		while(i > 0 && is_escaped(str, i)) i = str.find(delim, i+1);
	}
	if(i == -1) res.push_back(str);
	else{
		res.push_back(str.substr(0, i));
		if(escape) unescape_str(res.back());
		split_str(res, str.substr(i+1), delim, escape);
	}
}

// Basically equivalent to boost's join functionality
template<class Iterator>
std::string join_str(Iterator a, Iterator b, char delim, bool escape=true, const std::set<char>& esc={' '}){
	std::ostringstream ss;
	std::for_each(a, b, [&](std::string s){
		escape_str(s, esc);
		ss << s << delim;
	});
	return ss.str();
}

// Very fast way of checking if a file/directory exists
bool file_exists(const std::string& name){
	struct stat buffer;
	return stat(name.c_str(), &buffer) == 0;
}

// Load an entire file into a string. Whoop
std::string load_file(const std::string& filename){
	std::ostringstream ss;
	ss << std::ifstream(filename).rdbuf();
	return ss.str();
}

// Creates a directory with as many permissions as permitted
// Returns true if successful, false otherwise
bool make_dir(const std::string& dirname){
	return mkdir(dirname.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
}

//#include <iostream>
// Unnecessarily optimized function to read the last n lines of a file!
// I owe a lot of internet sources for this one. Primary info source:
// http://en.cppreference.com/w/cpp/io/basic_fstream
std::vector<std::string> load_file_ending(const std::string& filename, int n=20){
	std::ifstream file(filename);
	file.seekg(-1, std::ifstream::end);
	const std::streamoff len = file.tellg();

	std::vector<std::string> lines;
	lines.reserve(n);
	std::ostringstream ss;
	char c;
	int num_read = 0;
	for(int i=0; i<=len; ++i){
		c = file.get();
		//std::cout << "got: "<<c<<std::endl;
		if(c == '\n'){
			if(ss.tellp()){
				lines.push_back(ss.str());
				std::reverse(lines[num_read].begin(), lines[num_read].end());
				ss.clear();
				if(++num_read == n) break;
			}
		}
		else ss << c;
		file.seekg(-2, std::ifstream::cur);
	}
	if(ss.tellp()){
		lines.push_back(ss.str());
		std::reverse(lines[num_read].begin(), lines[num_read].end());
	}
	std::reverse(lines.begin(), lines.end());
	return lines;
}

// Utility to append a line to a file.  This is great for keeping track
// of timeline posts, since we can take the last lines 20 (by default)
void append_file(const std::string& filename, const std::string& str){
	std::ofstream(filename, std::fstream::in | std::fstream::app) << str << std::endl;
}

// Useful for overwriting outdated files
void overwrite_file(const std::string& filename, const std::string& str, bool delete_empty=true){
	if(str.empty() && delete_empty) remove(filename.c_str());
	else std::ofstream(filename, std::fstream::in) << str << std::endl;
}

std::string get_current_time(){
	auto tm_time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());;
	std::ostringstream ss;
	ss << std::put_time(localtime(&tm_time), "%d-%m-%Y %H-%M-%S");
	return ss.str();
}
#endif
