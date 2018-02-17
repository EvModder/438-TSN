#include <iostream>
#include <thread>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "tsn.grpc.pb.h"
#include "client.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
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


class Client : public IClient {
	private:
		string hostname;
		string username;
		string port;
		unique_ptr<SNetwork::Stub> stub_;
		map<string, IStatus> status_map;
	protected:
		virtual int connectTo();
		virtual IReply processCommand(string& input);
		virtual void processTimeline();
	public:
		Client(const string& hname, const string& uname, const string& p) : hostname(hname), username(uname), port(p) {
			status_map["SUCCESS"] = SUCCESS;
			status_map["FAILURE_ALREADY_EXISTS"] = FAILURE_ALREADY_EXISTS;
			status_map["FAILURE_NOT_EXISTS"] = FAILURE_NOT_EXISTS;
			status_map["FAILURE_INVALID_USERNAME"] = FAILURE_INVALID_USERNAME;
			status_map["FAILURE_INVALID"] = FAILURE_INVALID;
			status_map["FAILURE_UNKNOWN"] = FAILURE_UNKNOWN;
		}
};

int main(int argc, char** argv){
	// Defaults for lazy users
	string hostname = "localhost";
	string username = "default";
	string port = "12021";
	int opt;
	while((opt = getopt(argc, argv, "h:u:p:")) != -1){
		switch(opt) {
			case 'h':
				hostname = optarg;
				break;
			case 'u':
				username = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			default:
				cerr << "Invalid Command Line Argument\n";
		}
	}

	Client(hostname, username, port).run_client();
}

int Client::connectTo(){
	auto channel = CreateChannel(this->hostname + ":" + this->port, grpc::InsecureChannelCredentials());
	this->stub_ = tsn::SNetwork::NewStub(channel);

	// Attempt to create/join a user with the given name
	ClientContext context;
	CreateRequest request;
	CreateReply reply;

	request.set_username(this->username);
	Status status = this->stub_->create_user(&context, request, &reply);

	// If create_status is one of the two below, then the connection was successful
	if(status.ok()){
		string create_status = reply.status();
		if(create_status == "SUCCESS" || create_status == "FAILURE_ALREADY_EXISTS"){
			return 1;
		}
	}
	// There was an issue with grpc or we got a bad status
	return -1;
}

IReply Client::processCommand(string& input){
	// Grab the command out of the input string
	string command = input.substr(0, input.find(" "));
	string uname = "";

	// CAPS-ify the command
	//toUpperCase(command);
	//for(int i=0; command[i]; ++i) command[i] = toupper(command[i]);

	// Some generic GRPC variables for later use
	ClientContext context;
	Status status;

	// struct to be populated when the server sends a reply
	IReply ire;
	string server_status = "";

	if(command == "FOLLOW" || command == "UNFOLLOW"){
		// Get the username of the person we want to follow
		uname = input.substr(input.find(" ") + 1, input.length());

		PersonRequest request;
		request.set_requestuser(this->username);
		request.set_targetuser(uname);

		PersonReply reply;
		if(command == "FOLLOW")
			status = this->stub_->follow(&context, request, &reply);
		else
			status = this->stub_->unfollow(&context, request, &reply);

		ire.grpc_status = status;
		server_status = reply.status();
	}
	else if(command == "LIST"){
		ListRequest request;
		request.set_username(this->username);
		ListReply reply;
		status = this->stub_->list(&context, request, &reply);

		// Move all users to the ire struct
		for(string user : reply.users()) ire.all_users.push_back(user);

		// Move all followers to the ire struct
		for(string user : reply.followers()) ire.followers.push_back(user);

		ire.grpc_status = status;
		server_status = reply.status();

	}
	else if(command == "TIMELINE"){
		processTimeline();
		return ire;
	}
	// Set the appropriate status and return the IReply struct
	ire.comm_status = this->status_map[server_status];
	return ire;
}

void Client::processTimeline(){
	string uname = this->username;

	// Create the bidirectional stream
	ClientContext context;
	std::shared_ptr<ClientReaderWriter<TimelineStream, TimelineStream>> stream(stub_->timeline(&context));

	// Start of by just sending my username
	TimelineStream t;
	t.set_username(this->username);
	stream->Write(t);

	// Thread for user input
	thread user_in([uname, stream]() {
		TimelineStream t;
		string msg;
		while(true){
			msg = getPostMessage();
			t.set_username(uname);
			t.set_post(msg);
			t.set_time("");
			stream->Write(t);
		}
		stream->WritesDone();
	});

	// Thread for incoming message output
	thread serv_out([stream]() {
		TimelineStream t;
		while(stream->Read(&t)) {
			struct tm tm;
			strptime(t.time().c_str(), "%d-%m-%Y %H-%M-%S", &tm);
			time_t time = mktime(&tm);
			displayPostMessage(t.username(), t.post(), time);
		}
	});

	// Block until both threads have finished.
	// Since that will never happen, this effectively keeps us in Timeline-mode
	// Thus, to exit the program from this state you must run Ctrl-C
	user_in.join();
	serv_out.join();
}
