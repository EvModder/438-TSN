#include <iostream>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "tsn.grpc.pb.h"
#include "utils.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using tsn::CreateRequest;
using tsn::CreateReply;
using tsn::PersonRequest;
using tsn::PersonReply;
using tsn::ListRequest;
using tsn::ListReply;
using tsn::TimelineStream;
using tsn::SNetwork;

using namespace std;

typedef ServerReaderWriter<TimelineStream, TimelineStream>* timeline_stream;

// Handy struct for TimelinePost TODO: nuke?
struct TimelinePost{
	string username;// The Poster
	string post;// Content of post
	string time;// Time content was posted
};

// Map for quick lookup of a user
unordered_map<string, timeline_stream> users;

// Folder in which TSN data is stored
const string GLOBAL_DIR = "./server_data/";

// Load user accounts on startup
void load_users(){
	// Create data folder if not already present
	if(!file_exists(GLOBAL_DIR)) make_dir(GLOBAL_DIR);

	// Read in a list of user account names
	string str = load_file(GLOBAL_DIR + "userlist.txt");
	vector<string> usernames;
	split_str(usernames, str, '\n');
	for(string name : usernames){
		if(!name.empty()) users[name] = nullptr;
	}
}

vector<string> get_followers(string user){
	string file_contents = load_file(GLOBAL_DIR + user + "/followers.txt");
	cout << "Value of get_followers() for " << user << ": " << file_contents << endl;
	vector<string> followers;
	split_str(followers, file_contents, ' ');
	return followers;
}

bool is_following(string follower, string followed){
	vector<string> fs = get_followers(followed);
	return find(fs.begin(), fs.end(), follower) != fs.end();
}

vector<string> get_timeline(string user){
	return load_file_ending(GLOBAL_DIR + user + "/timeline.txt", 20);
}

class SNetworkServiceImpl final : public SNetwork::Service {
	Status create_user(ServerContext* context, const CreateRequest* request, CreateReply* reply) override {
		string username = request->username();

		// If the user is already logged on from elsewhere
		if(users.count(username) > 0){
			reply->set_status("FAILURE_ALREADY_EXISTS");
			return Status::OK;
		}
		if(username.empty()){
			reply->set_status("FAILURE_INVALID_USERNAME");
			return Status::OK;
		}

		// If this is a new user, initialize his/her data folder
		if(!file_exists(GLOBAL_DIR + username)){
			make_dir(GLOBAL_DIR + username);
		}

		// Keep track of this user from now on (officially in the system)
		append_file(GLOBAL_DIR + "userlist.txt", username);
		users[username] = nullptr;

		reply->set_status("SUCCESS");
		return Status::OK;
	}

	Status follow(ServerContext* context, const PersonRequest* request, PersonReply* reply) override {
		string watcher = request->requestuser();
		string watched = request->targetuser();

		// Can't follow yourself or someone who doesn't exist!
		if(watcher == watched || !users.count(watched)){
			reply->set_status("FAILURE_INVALID_USERNAME");
			return Status::OK;
		}

		// You are already following this person!
		if(is_following(watcher, watched)){
			reply->set_status("FAILURE_ALREADY_EXISTS");
			return Status::OK;
		}

		// Good to go! Add this person as a follower
		append_file(GLOBAL_DIR + watched + "/followers.txt", watcher);
		reply->set_status("SUCCESS");
		return Status::OK;
	}

	Status unfollow(ServerContext* context, const PersonRequest* request, PersonReply* reply) override {
		string watcher = request->requestuser();
		string watched = request->targetuser();

		// Can't unfollow yourself or someone who doesn't exist!
		if(watcher == watched || !users.count(watched)){
			reply->set_status("FAILURE_INVALID_USERNAME");
			return Status::OK;
		}

		// Can't unfollow someone who you aren't following!
		vector<string> fs = get_followers(watched);
		set<string> followers(fs.begin(), fs.end());
		if(followers.count(watcher)){
			reply->set_status("FAILURE_INVALID");
			return Status::OK;
		}

		// Good to go! Remove from the follower list
		followers.erase(watcher);
		string new_list = join_str(followers.begin(), followers.end(), '\n');
		overwrite_file(GLOBAL_DIR + watched + "/followers.txt", new_list);

		reply->set_status("SUCCESS");
		return Status::OK;
	}

	Status list(ServerContext* context, const ListRequest* request, ListReply* reply) override {
		// Pretty straightforward. Send back a reply containing two lists,
		// a lists of all users and a list of this person's followers
		for(auto user : users){
			reply->add_users(user.first);
		}
		for(string follower : get_followers(request->username())){
			reply->add_followers(follower);
		}
		reply->set_status("SUCCESS");
		return Status::OK;
	}

	Status timeline(ServerContext* context, timeline_stream stream) override {
		// Read a message containing the username for this stream
		TimelineStream user;
		stream->Read(&user);
		string username = user.username();

		// Hold onto this stream pointer so we can get the freshest posts
		users[username] = stream;

		// Give them the last 20 events from their timeline
		vector<string> lines = load_file_ending(GLOBAL_DIR + username + "/timeline.txt", 20);
		for(string line : lines){
			/*TODO
			TimelineStream send_obj;
			send_obj.set_username(users[current_user].timeline[i].username);
			send_obj.set_post(users[current_user].timeline[i].post);
			send_obj.set_time(users[current_user].timeline[i].time);
			stream->Write(send_obj);
			*/
		}

		TimelineStream t;
		while(stream->Read(&t)){
			string poster = t.username();
			string content = t.post();

			// Build the TimelinePost object from the incoming message
			TimelinePost new_post;
			new_post.username = poster;
			new_post.post = content;
			new_post.time = get_current_time();
			vector<string> post_vec {poster, content, new_post.time};
			set<char> sep {'|'};
			string post_str = join_str(post_vec.begin(), post_vec.end(), '|', true, sep);

			// Add this post to my timeline
			cout << "Incoming post from: " << poster << endl;
			cout << "Stringified-post: " << post_str << endl;
			append_file(GLOBAL_DIR + poster + "/timeline.txt", post_str);

			// Send this post out to all the followers
			for(string follower : get_followers(poster)){
				// Add this post to the follower's timeline
				append_file(GLOBAL_DIR + follower + "/timeline.txt", post_str);

				// If the follower is online, also stick this in his/her stream
				if(users[follower] != nullptr) users[follower]->Write(t);
			}
		}

		// If we've exited the while and dropped down here, the user disconnected.
		// Update the user stream (timeline will still get updates)
		users[username] = nullptr;
		return Status::OK;
	}
};

int main(int argc, char** argv){
	// Default values in case the user doesn't supply anything
	string hostname = "localhost";
	string port = "12021";
	int opt;
	while((opt = getopt(argc, argv, "h:u:p:")) != -1) {
		switch(opt) {
			case 'h':
				hostname = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			default:
				cerr << "Invalid Command Line Argument\n";
		}
	}

	load_users();

	// Launch the grpc server
	ServerBuilder builder;
	builder.AddListeningPort(hostname+":"+port, grpc::InsecureServerCredentials());
	SNetworkServiceImpl service;
	builder.RegisterService(&service);
	unique_ptr<Server>(builder.BuildAndStart())->Wait();
}
