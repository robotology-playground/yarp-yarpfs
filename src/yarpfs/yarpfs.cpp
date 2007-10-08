// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2007 Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the GNU GPL v2.0.
 *
 */

#define FUSE_USE_VERSION 30

#include <fuse/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
//#include <fcntl.h>

#include <yarp/os/all.h>
#include <yarp/NameClient.h>
#include <yarp/NameConfig.h>

#include <string>
#include <signal.h>

#include <ace/Containers_T.h>

using namespace yarp::os;
using namespace yarp;
using namespace std;


/*

We try to map from filesystem paths to port names.

The "directory" is the port name, and the "file" part is
an aspect of the port - properties, or read/write pipes.

 */
class YPath {
private:
    string path, head, tail;
    bool dirty;
    bool _isStem, _isPort, _isAct;

    void check() {
        if (dirty) {
            NameConfig nc;
            String name = nc.getNamespace();
            Bottle msg, reply;
            msg.addString("bot");
            msg.addString("query");
            msg.addString(head.c_str());
            Network::write(name.c_str(),
                           msg,
                           reply);
            printf("Check got %s\n", reply.toString().c_str());
            _isAct = false;
            _isStem = true;
            _isPort = false;
            if (!reply.check("error")) {
                _isAct = true;
                _isStem = false;
                _isPort = false;
            } else {
                msg.clear();
                msg.addString("bot");
                msg.addString("query");
                msg.addString(path.c_str());
                Network::write(name.c_str(),
                               msg,
                               reply);
                printf("Check 2 got %s\n", reply.toString().c_str());
                if (!reply.check("error")) {
                    _isAct = false;
                    _isStem = true;
                    _isPort = true;
                }
            }
            dirty = false;
        }
    }
public:
    YPath(const char *path) {
        set(path);
    }

    void set(const char *path) {
        this->path = path;
        head = path;
        tail = path;
        size_t at = tail.rfind("/");
        if (at!=tail.npos) {
            tail = tail.substr(at+1,tail.length());
            head = head.substr(0,at);
        }
        printf("PATH [%s] [%s]\n", head.c_str(), tail.c_str());
        dirty = true;
    }

    string getHead() {
        return head;
    }

    string getTail() {
        return tail;
    }

    bool isStem() {
        check();
        return _isStem;
    }

    bool isPort() {
        check();
        return _isPort;
    }

    bool isAct() {
        check();
        return _isAct;
    }
};

static int yarp_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    YPath ypath(path);

    memset(stbuf, 0, sizeof(struct stat));
    if (ypath.isStem()) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (ypath.isAct()) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 0; //strlen(yarp_str);
    }
    else {
        res = -ENOENT;
    }

    return res;
}

static int yarp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    printf(">>>>>>>>>>>READING DIR\n");


    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    YPath ypath(path);

    if (ypath.isPort()) {
        filler(buf, "read", NULL, 0);
        filler(buf, "write", NULL, 0);
        filler(buf, "status", NULL, 0);
        return 0;
    }

    NameConfig nc;
    
    String name = nc.getNamespace();
    Bottle msg, reply;
    msg.addString("bot");
    msg.addString("list");
    Network::write(name.c_str(),
                   msg,
                   reply);

    printf("Got %s\n", reply.toString().c_str());

    ACE_Ordered_MultiSet<String> lines;


    for (int i=1; i<reply.size(); i++) {
        Bottle *entry = reply.get(i).asList();
        if (entry!=NULL) {
            ConstString name = entry->check("name",Value("")).asString();
            if (name!="") {
                if (strstr(name.c_str(),path)==name.c_str()) {
                    printf(">>> %s is in path %s\n", name.c_str(),path);
                    String part(name.c_str()+strlen(path));
                    if (part[0]=='/') {
                        part = part.substr(1,part.length()-1);
                    }
                    printf("    %s is the addition\n", part.c_str());

                    char *brk = (char*)strstr(part.c_str(),"/");
                    if (brk!=NULL) {
                        *brk = '\0';
                    }
                    String item(part.c_str());
                    printf("   %s is the item\n", item.c_str());
                    if (item!="") {
                        lines.remove(item);
                        lines.insert(item);
                    }
                }
            }
        }
    }

    // return result in alphabetical order
    ACE_Ordered_MultiSet_Iterator<String> iter(lines);
    iter.first();
    while (!iter.done()) {
        printf("adding item %s\n", (*iter).c_str());
        filler(buf, (*iter).c_str(), NULL, 0);
        iter.advance();
    }


    return 0;
}

static int yarp_open(const char *path, struct fuse_file_info *fi)
{
    //if(strcmp(path, "/nonexist") != 0)
    //return -ENOENT;

    //if((fi->flags & 3) != O_RDONLY)
    //return -EACCES;

    fi->direct_io = 1;

    return 0;
}


static void alarm_handler(int x) {
    printf("PANIC!!!\n");
}


static int yarp_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    size_t len;
    (void) fi;


    YPath ypath(path);

    string tail = ypath.getTail();
    string head = ypath.getHead();
    // lets just try to implement read for now
    if (tail!="read") {
        return -ENOENT;
    }

    signal(SIGALRM, &alarm_handler);
    signal(SIGPIPE, &alarm_handler);
    signal(SIGTERM, &alarm_handler);

    BufferedPort<Bottle> port;
    port.open("...");
    Network::connect(head.c_str(),port.getName());
    Bottle *bot = port.read();
    port.close();
    if (bot==NULL) {
        return -ENOENT;
    }
    string str = bot->toString().c_str();
    str = str + "\n";
    const char *yarp_str = str.c_str();
    printf(">>> Got %s\n", str.c_str());

    offset = 0;
    len = strlen(yarp_str);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, yarp_str + offset, size);
    } else
        size = 0;

    return size;
}

static void *yarp_init(struct fuse_conn_info *conn) {
    umask(0);
    Network yarp;
    printf("Initializing...\n");
    return NULL;
}


static struct fuse_operations yarp_oper;

int main(int argc, char *argv[])
{
    yarp_oper.getattr	= yarp_getattr;
    yarp_oper.readdir	= yarp_readdir;
    yarp_oper.open	= yarp_open;
    yarp_oper.read	= yarp_read;
    yarp_oper.init       = yarp_init;
    return fuse_main(argc, argv, &yarp_oper,NULL);
}

