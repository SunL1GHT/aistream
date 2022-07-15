/****************************************************************************************
 * Copyright (C) 2021 aistream <aistream@yeah.net>
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this 
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 ***************************************************************************************/

#include "stream.h"
#include "cJSON.h"

static auto GetLowestSlave(MasterParams* master) {
    std::shared_ptr<SlaveParam> slave = nullptr;
    master->m_slave_mtx.lock();
    for(size_t i = 0; i < master->m_slave_vec.size(); i++) {
        auto _slave = master->m_slave_vec[i];
        if(_slave->alive && _slave->load.total_load < SLAVE_LOAD_MAX) {
            if(slave == nullptr || _slave->load.total_load < slave->load.total_load) {
                slave = _slave;
            }
        }
    }
    master->m_slave_mtx.unlock();
    return slave;
}

static bool AssignObjToSlave(auto obj, MasterParams* master) {
    char url[256];
    HttpAck ack = {nullptr};

    // get lowest slave
    auto slave = GetLowestSlave(master);
    if(slave == nullptr) {
        //AppWarn("get lowest slave failed, id:%d", obj->id);
        return false;
    }
    // add obj to slave
    auto type = GetStrValFromJson(obj->params.get(), "type");
    if(type == nullptr) {
        AppWarn("get obj type failed, id:%d", obj->id);
        return false;
    }
    snprintf(url, sizeof(url), "http://%s:%d/api/obj/add/%s", 
             slave->ip, slave->rest_port, type.get());
    HttpPost(url, obj->params.get(), &ack);
    // update load
    if(ack.buf != nullptr) {
        double load = GetDoubleValFromJson(ack.buf.get(), "data", "load");
        if(load >= 0) {
            slave->load.total_load = load;
        }
        else {
            AppWarn("slave:%s, get load from ack failed, %s", slave->ip, ack.buf.get());
        }
        obj->slave = slave;
        AppDebug("assign obj %d to %s:%d", obj->id, slave->ip, slave->rest_port);
    }
    else {
        slave->alive = false;
        AppWarn("slave %s:%d is offline", slave->ip, slave->rest_port);
        return false;
    }

    return true;
}

static int HttpStart(auto obj, char* buf, const char* url, HttpAck* ack, MasterParams* master) {
    char _url[256];
    if(obj->slave == nullptr) {
        if(AssignObjToSlave(obj, master) != true) {
            printf("assign obj %d to slave failed\n", obj->id);
            obj->status = 0;
            return -1;
        }
    }
    if(!obj->slave->alive) {
        obj->status = 0;
        return -1;
    }
    obj->status = 1;
    snprintf(_url, sizeof(_url), "http://%s:%d%s", obj->slave->ip, obj->slave->rest_port, url);
    HttpPost(_url, buf, ack);
    return 0;
}

static int HttpStop(auto obj, char* buf, const char* url, 
                    HttpAck* ack, MasterParams* master, int timeout_sec = 5) {
    char _url[256];
    if(obj->slave == nullptr) {
        //printf("http stop, obj slave is null, id:%d\n", obj->id);
        return -1;
    }
    if(!obj->slave->alive) {
        return -1;
    }
    snprintf(_url, sizeof(_url), "http://%s:%d%s", obj->slave->ip, obj->slave->rest_port, url);
    HttpPost(_url, buf, ack, timeout_sec);
    return 0;
}

static int HttpToSlave(char* buf, const char* url, MasterParams* master) {
    char _url[256];
    HttpAck ack = {nullptr};
    master->m_slave_mtx.lock();
    for(size_t i = 0; i < master->m_slave_vec.size(); i++) {
        auto slave = master->m_slave_vec[i];
        if(!slave->alive) {
            continue;
        }
        snprintf(_url, sizeof(_url), "http://%s:%d%s", slave->ip, slave->rest_port, url);
        HttpPost(_url, buf, &ack);
    }
    master->m_slave_mtx.unlock();
    return 0;
}

static int SlaveSystemInit(auto slave, MasterParams* master) {
    char url[256];
    char buf[384];
    HttpAck ack = {nullptr};
    const char *sys_url = "/api/system/init";

    int timeout_sec = slave->alive ? 3 : 1;
    snprintf(url, sizeof(url), "http://%s:%d%s", slave->ip, slave->rest_port, sys_url);
    snprintf(buf, sizeof(buf), "{\"ip\":\"%s\",\"internet_ip\":\"%s\"}", slave->ip, slave->internet_ip);
    HttpPost(url, buf, &ack, timeout_sec);
    if(ack.buf == nullptr) {
        slave->alive = false;
        return -1;
    }
    slave->alive = true;

    if(master->output == nullptr) {
        return -1;
    }
    sys_url = "/api/system/set/output";
    snprintf(url, sizeof(url), "http://%s:%d%s", slave->ip, slave->rest_port, sys_url);
    HttpPost(url, master->output.get(), &ack, timeout_sec);

    return 0;
}

static std::shared_ptr<SlaveParam> GetSlave(char* ip, MasterParams* master) {
    std::shared_ptr<SlaveParam> slave = nullptr;
    master->m_slave_mtx.lock();
    for(size_t i = 0; i < master->m_slave_vec.size(); i++) {
        auto _slave = master->m_slave_vec[i];
        if(!strncmp(_slave->ip, ip, sizeof(_slave->ip))) {
            slave = _slave;
            break;
        }
    }
    master->m_slave_mtx.unlock();
    return slave;
}

static auto MAddSlave(char* buf, MasterParams* master) {
    std::shared_ptr<SlaveParam> slave = nullptr;
    auto ip = GetStrValFromJson(buf, "ip");
    auto internet_ip = GetStrValFromJson(buf, "internet_ip");
    int rest_port = GetIntValFromJson(buf, "rest_port");
    if(ip == nullptr || rest_port < 0) {
        AppWarn("get ip/port failed");
        return slave;
    }
    auto _slave = GetSlave(ip.get(), master);
    if(_slave != nullptr) {
        AppWarn("slave %s exist", ip.get());
        return slave;
    }
    slave = std::make_shared<SlaveParam>();
    slave->params = std::make_unique<char[]>(strlen(buf)+1);
    strcpy(slave->params.get(), buf);
    strncpy(slave->ip, ip.get(), sizeof(slave->ip));
    if(internet_ip != nullptr) {
        strncpy(slave->internet_ip, internet_ip.get(), sizeof(slave->internet_ip));
    }
    slave->rest_port = rest_port;
    master->m_slave_mtx.lock();
    master->m_slave_vec.push_back(slave);
    master->m_slave_mtx.unlock();
    return slave;
}

static int MDelSlave(char* ip, MasterParams* master) {
    master->m_slave_mtx.lock();
    for(auto itr = master->m_slave_vec.begin(); itr != master->m_slave_vec.end(); ++itr) {
        if(!strncmp((*itr)->ip, ip, sizeof((*itr)->ip))) {
            (*itr)->alive = false;
            master->m_slave_vec.erase(itr);
            break;
        }
    }
    master->m_slave_mtx.unlock();
    return 0;
}

static std::shared_ptr<MObjParam> GetMObj(int id, MasterParams* master) {
    std::shared_ptr<MObjParam> obj = nullptr;
    master->m_obj_mtx.lock();
    for(size_t i = 0; i < master->m_obj_vec.size(); i++) {
        auto _obj = master->m_obj_vec[i];
        if(_obj->id == id) {
            obj = _obj;
            break;
        }
    }
    master->m_obj_mtx.unlock();
    return obj;
}

static auto MAddObj(char* buf, MasterParams* master) {
    auto obj = std::make_shared<MObjParam>();
    obj->id = GetIntValFromJson(buf, "id");
    obj->params = std::make_unique<char[]>(strlen(buf)+1);
    strcpy(obj->params.get(), buf);
    master->m_obj_mtx.lock();
    master->m_obj_vec.push_back(obj);
    master->m_obj_mtx.unlock();
    return obj;
}

static int MDelObj(int id, MasterParams* master) {
    master->m_obj_mtx.lock();
    for(auto itr = master->m_obj_vec.begin(); itr != master->m_obj_vec.end(); ++itr) {
        if((*itr)->id == id) {
            master->m_obj_vec.erase(itr);
            break;
        }
    }
    master->m_obj_mtx.unlock();
    return 0;
}

void MObjParam::AddTask(char* params) {
    std::string _params(params);
    m_task_mtx.lock();
    m_task_vec.push_back(_params);
    m_task_mtx.unlock();
}

bool MObjParam::DelTask(char* name) {
    bool find = false;
    m_task_mtx.lock();
    for(auto itr = m_task_vec.begin(); itr != m_task_vec.end(); ++itr) {
        const char* _params = (*itr).c_str();
        auto _name = GetStrValFromJson((char* )_params, "task");
        if(_name != nullptr && !strcmp(name, _name.get())) {
            m_task_vec.erase(itr);
            find = true;
            break;
        }
    }
    m_task_mtx.unlock();
    return find;
}

std::unique_ptr<char[]> MObjParam::GetTask(void) {
    std::unique_ptr<char[]> params = nullptr;
    m_task_mtx.lock();
    if(m_task_vec.size() > 0) {
        const char* _params = m_task_vec[0].c_str();
        params = std::make_unique<char[]>(strlen(_params) + 1);
        strcpy(params.get(), _params);
    }
    m_task_mtx.unlock();
    return params;
}

static int SystemInitCB(char* buf, void* arg) {
    MediaServer *media = (MediaServer *)arg;
    MasterParams* master = media->GetMaster();
    auto output = GetObjBufFromJson(buf, "output", "data");
    if(output != nullptr) {
        master->output = std::make_unique<char[]>(strlen(output.get())+1);
        strcpy(master->output.get(), output.get());
    }
    return 0;
}

static void GetSlaveObj(auto slave) {
    char url[256];
    HttpAck ack = {nullptr};
    const char *_url = "/api/obj/status";
    snprintf(url, sizeof(url), "http://%s:%d%s", slave->ip, slave->rest_port, _url);
    HttpGet(url, &ack);
    if(ack.buf != nullptr) {
        int size = 0;
        auto array = GetArrayBufFromJson(ack.buf.get(), size, "data", "obj");
        if(array == nullptr || size == 0) {
            return;
        }
        for(int i = 0; i < size; i++) {
            auto arrbuf = GetBufFromArray(array.get(), i);
            if(arrbuf == NULL) {
                continue;
            }
            int id = GetIntValFromJson(arrbuf.get(), "id");
            if(id >= 0) {
                slave->obj_id_vec.push_back(id);
            }
        }
    }
}

static int SlaveCB(char* buf, void* arg) {
    MediaServer *media = (MediaServer *)arg;
    MasterParams* master = media->GetMaster();

    auto ip = GetStrValFromJson(buf, "ip");
    if(ip == nullptr) {
        return -1;
    }
    auto slave = MAddSlave(buf, master);
    if(slave == nullptr) {
        return -1;
    }

    char url[256];
    HttpAck ack = {nullptr};
    const char* sys_url = "/api/system/status";
    snprintf(url, sizeof(url), "http://%s:%d%s", slave->ip, slave->rest_port, sys_url);
    HttpGet(url, &ack);
    if(ack.buf != nullptr) {
        slave->alive = true;
        int system_init = GetIntValFromJson(ack.buf.get(), "data", "system_init");
        // when restart master and slave is running, don't init repeatly
        if(system_init == 0) {
            SlaveSystemInit(slave, master);
        }
        // get slave assigned obj(master restarted), it will be used in GetSlaveById, 
        GetSlaveObj(slave);
    }
    AppDebug("ip:%s,port:%d, alive:%d", slave->ip, slave->rest_port, slave->alive);

    return 0;
}

static auto GetSlaveById(int id, MasterParams* master) {
    std::shared_ptr<SlaveParam> slave = nullptr;
    master->m_slave_mtx.lock();
    for(size_t i = 0; i < master->m_slave_vec.size()&&slave == nullptr; i++) {
        auto _slave = master->m_slave_vec[i];
        for(size_t j = 0; j < _slave->obj_id_vec.size(); j++) {
            int _id = _slave->obj_id_vec[j];
            if(id == _id) {
                slave = _slave;
                break;
            }
        }
    }
    master->m_slave_mtx.unlock();
    return slave;
}

static int ObjCB(char* buf, void* arg) {
    MediaServer *media = (MediaServer *)arg;
    MasterParams* master = media->GetMaster();

    auto _buf = DelJsonObj(buf, "_id");
    if(_buf != nullptr) {
        buf = _buf.get();
    }
    // add obj
    auto obj = MAddObj(buf, master);
    if(obj == nullptr) {
        AppWarn("add obj failed");
        return -1;
    }
    // check if this obj is already running in someone obj,
    // avoid adding obj to slave repeatly
    obj->slave = GetSlaveById(obj->id, master);

    // add task
    int size = 0;
    auto array = GetArrayBufFromJson(buf, size, "data", "task");
    if(array == nullptr || size == 0) {
        return 0;
    }
    for(int i = 0; i < size; i++) {
        auto _params = GetBufFromArray(array.get(), i);
        if(_params == nullptr) {
            continue;
        }
        obj->AddTask(_params.get());
    }

    return 0;
}

void request_gencb(struct evhttp_request* req, void* arg) {
    char remote_ip[64] = {0};
    const char* uri = evhttp_request_get_uri(req);

    if(req->remote_host != NULL) {
        strncpy(remote_ip, req->remote_host, sizeof(remote_ip));
    }
    if(evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
        printf("request_gencb failed, only support http get,%d, "
                "url:%s, remote:%s\n", evhttp_request_get_command(req), uri, remote_ip);
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }
    if(memcmp(uri, "/img", strlen("/img")) != 0) {
        printf("http get failed, url:%s, remote:%s\n", uri, remote_ip);
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }
    //printf("http general get : %s\n",  uri);

    std::string filepath = uri;
    evkeyvalq* outhead = evhttp_request_get_output_headers(req);
    int pos = filepath.rfind('.');
    std::string postfix = filepath.substr(pos + 1, filepath.size() - (pos + 1));
    if(postfix == "jpg"||postfix == "gif"||postfix == "png") {
        std::string tmp = "image/" + postfix;
        evhttp_add_header(outhead, "Content-Type", tmp.c_str());
    }

    char buf[1024] = { 0 };
    std::string img_path = "./data" + filepath;
    FILE* fp = fopen(img_path.c_str(), "rb");
    if(!fp) {
        evhttp_send_reply(req, HTTP_NOTFOUND, "", 0);
        return;
    }
    evbuffer* outbuf = evhttp_request_get_output_buffer(req);
    for(;;) {
        int len = fread(buf, 1, sizeof(buf), fp);
        if (len <= 0)break;
        evbuffer_add(outbuf, buf, len);
    }
    fclose(fp);
    evhttp_send_reply(req, HTTP_OK, "", outbuf);
}

/**********************************************************
{
  "username": "admin",
  "password": "123456"
}
**********************************************************/
static void request_login(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    auto username = GetStrValFromJson(buf, "username");
    auto password = GetStrValFromJson(buf, "password");
    if(username == nullptr || password == nullptr || 
            strcmp(username.get(), "admin") != 0 || 
            strcmp(password.get(), "123456") != 0) {
        CheckErrMsg("username/password is error", (char** )params->argb);
    }
}

static void request_logout(struct evhttp_request* req, void* arg) {
    request_first_stage;
}

static void request_system_init(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();

    char tmp[512];
    const char* buf = "{}";
    snprintf(tmp, sizeof(tmp), "{\"init\":{\"flag\":1,\"data\":%s}}", buf);
    db->DBUpdate("system", tmp, "init.flag", 1);
}

/**********************************************************
{
    "type": "mq",
    "data": {
        "host": "192.168.0.10",
        "port": 5672,
        "username": "guest",
        "password": "guest",
        "exchange": "amq.direct",
        "routingkey": ""
    }
}
**********************************************************/
static void request_set_output(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char tmp[512];
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    master->output = std::make_unique<char[]>(strlen(buf)+1);
    strcpy(master->output.get(), buf);
    HttpToSlave(buf, "/api/system/set/output", master);
    snprintf(tmp, sizeof(tmp), "{\"output\":{\"flag\":1,\"data\":%s}}", buf);
    db->DBUpdate("system", tmp, "output.flag", 1);
}

/**********************************************************
{
    "ip":"192.168.0.100",
    "rest_port":8082,
    "internet_ip":"8.8.8.8"
}
**********************************************************/
static void request_add_slave(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char err_msg[128] = {0};
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    auto ip = GetStrValFromJson(buf, "ip");
    auto slave = MAddSlave(buf, master);
    if(slave == nullptr || ip == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "add slave failed");
        goto end;
    }
    SlaveSystemInit(slave, master);
    db->DBUpdate("slave", buf, "ip", ip.get());
end:
    CheckErrMsg(err_msg, (char **)params->argb);
}

/**********************************************************
{
    "ip":"192.168.0.100"
}
**********************************************************/
static void request_del_slave(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char err_msg[128] = {0};
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    auto ip = GetStrValFromJson(buf, "ip");
    if(ip == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "get ip failed");
        goto end;
    }
    MDelSlave(ip.get(), master);
    db->DBDel("slave", "ip", ip.get());
end:
    CheckErrMsg(err_msg, (char **)params->argb);
}

/**********************************************************
{
  "id":99,
  "data":{
    "tcp_enable":0,
    "url":"rtsp://127.0.0.1:8554/test.264"
  }
} 
**********************************************************/
static void request_add_rtsp(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char err_msg[128] = {0};
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    int id;
    std::shared_ptr<MObjParam> obj;
    auto _buf = AddStrJson(buf, "rtsp", "type");
    if(_buf == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "parse json failed");
        goto end;
    }
    buf = _buf.get();
    id = GetIntValFromJson(buf, "id");
    if(id <= 0) {
        snprintf(err_msg, sizeof(err_msg), "get id failed");
        goto end;
    }
    obj = GetMObj(id, master);
    if(obj != nullptr) {
        snprintf(err_msg, sizeof(err_msg), "obj %d exist", id);
        goto end;
    }
    if(MAddObj(buf, master) == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "add obj failed");
        goto end;
    }
    db->DBUpdate("obj", buf, "id", id);
end:
    CheckErrMsg(err_msg, (char **)params->argb);
}

/**********************************************************
{
  "id":99,
} 
**********************************************************/
static void request_del_obj(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char err_msg[128] = {0};
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    HttpAck ack = {nullptr};
    std::shared_ptr<MObjParam> obj;
    int id = GetIntValFromJson(buf, "id");
    if(id < 0) {
        snprintf(err_msg, sizeof(err_msg), "get id failed");
        goto end;
    }
    obj = GetMObj(id, master);
    if(obj == nullptr) {
        AppWarn("get obj %d exist", id);
        goto end;
    }
    HttpStop(obj, buf, "/api/obj/del", &ack, master, 30);
    if(ack.buf != nullptr) {
        double load = GetDoubleValFromJson(ack.buf.get(), "data", "load");
        if(load >= 0) {
            obj->slave->load.total_load = load;
        }
    }
    MDelObj(id, master);
    db->DBDel("obj", "id", id);
end:
    CheckErrMsg(err_msg, (char **)params->argb);
}

/**********************************************************
{
  "id":99,
  "data":{
    "task":"yolov3",
    "params":{
        "preview": "hls"/"http-flv"/"none",
        "record": "mp4"/"h264"/"none"
    }
  }
} 
**********************************************************/
static void request_start_task(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char err_msg[128] = {0};
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    HttpAck ack = {nullptr};
    std::shared_ptr<MObjParam> obj;
    int id = GetIntValFromJson(buf, "id");
    auto _params = GetObjBufFromJson(buf, "data");
    if(id < 0 || _params == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "get id or task params failed");
        goto end;
    }
    obj = GetMObj(id, master);
    if(obj == nullptr) {
        AppWarn("get obj %d failed", id);
        goto end;
    }
    obj->AddTask(_params.get());
    HttpStart(obj, buf, "/api/task/start", &ack, master);
    db->DBUpdate("obj", "id", id, "data.task", _params.get(), "$push");
end:
    CheckErrMsg(err_msg, (char **)params->argb);
}

/**********************************************************
{
  "id":99,
  "data":{
    "task":"yolov3"
  }
} 
**********************************************************/
static void request_stop_task(struct evhttp_request* req, void* arg) {
    request_first_stage;
    char err_msg[128] = {0};
    CommonParams* params = (CommonParams* )arg;
    char* buf = (char* )params->arga;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    DbParams* db = media->GetDB();
    MasterParams* master = media->GetMaster();

    HttpAck ack = {nullptr};
    std::shared_ptr<MObjParam> obj;
    int id = GetIntValFromJson(buf, "id");
    auto task = GetStrValFromJson(buf, "data", "task");
    auto _data = GetObjBufFromJson(buf, "data");
    if(id < 0 || task == nullptr || _data == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "get id/task failed");
        goto end;
    }
    obj = GetMObj(id, master);
    if(obj == nullptr) {
        snprintf(err_msg, sizeof(err_msg), "get obj %d failed", id);
        goto end;
    }
    if(!obj->DelTask(task.get())) {
        snprintf(err_msg, sizeof(err_msg), "del task failed, id:%d, task:%s", id, task.get());
        goto end;
    }
    HttpStop(obj, buf, "/api/task/stop", &ack, master, 30);
    db->DBUpdate("obj", "id", id, "data.task", _data.get(), "$pull");
    obj->status = 0;
end:
    CheckErrMsg(err_msg, (char **)params->argb);
}

static void request_task_support(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    char* url = (char *)params->arge;
    char** ppbody = (char** )params->argb;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    MasterParams* master = media->GetMaster();

    cJSON* root = cJSON_CreateObject();
    cJSON* data_root =  cJSON_CreateObject();
    cJSON* alg_root =  cJSON_CreateArray();
    cJSON_AddStringToObject(root, "code", "0");
    cJSON_AddStringToObject(root, "msg", "success");
    cJSON_AddItemToObject(root, "data", data_root);
    cJSON_AddItemToObject(data_root, "alg", alg_root);
    //task support;
    const char *flag = "/api/task/support?obj=";
    char *obj = strstr(url, flag);
    if(obj == NULL) {
        goto end;
    }
    //obj = url + strlen(flag); //TODO:return task determined by obj type
    for(size_t i = 0; i < master->cfg_task_vec.size(); i ++) {
        cJSON *fld;
        cJSON_AddItemToArray(alg_root, fld = cJSON_CreateObject());
        cJSON_AddStringToObject(fld, "name", master->cfg_task_vec[i].c_str());
        cJSON_AddNumberToObject(fld, "disabled", 0);
    }
end:
    *ppbody = cJSON_Print(root);
    cJSON_Delete(root);
}

static void request_admin_info(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    const char* msg = "{\"status\":1,\"data\":{\"user_name\":\"admin\",\"id\":12,\"create_time\":"
        "\"2017-12-03 08:30\",\"status\":1,\"city\":\"nj\",\"avatar\":\"default.png\",\"admin\":\"\"}}";
    SetAckMsg(msg, (char **)params->argb);
}

static void request_system_info(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    char** ppbody = (char** )params->argb;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    MasterParams* master = media->GetMaster();

    // create system info json base
    cJSON* root = cJSON_CreateObject();
    cJSON* data_root =  cJSON_CreateObject();
    cJSON* output_root =  cJSON_CreateObject();
    cJSON* build_root =  cJSON_CreateObject();
    cJSON_AddStringToObject(root, "code", "0");
    cJSON_AddStringToObject(root, "msg", "success");
    cJSON_AddItemToObject(root, "data", data_root);
    cJSON_AddItemToObject(data_root, "build", build_root);
    cJSON_AddItemToObject(data_root, "output", output_root);
    // add build info
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s %s", __TIME__, __DATE__);
    cJSON_AddStringToObject(build_root, "version", SW_VERSION);
    cJSON_AddStringToObject(build_root, "time", tmp);
    // add output
    if(master->output != nullptr) {
        char* buf = master->output.get();
        auto type = GetStrValFromJson(buf, "type");
        auto host = GetStrValFromJson(buf, "data", "host");
        int port = GetIntValFromJson(buf, "data", "port");
        auto username = GetStrValFromJson(buf, "data", "username");
        auto password = GetStrValFromJson(buf, "data", "password");
        auto exchange = GetStrValFromJson(buf, "data", "exchange");
        auto routingkey = GetStrValFromJson(buf, "data", "routingkey");
        if(type != nullptr && host != nullptr && username != nullptr &&
          password != nullptr && exchange != nullptr && routingkey != nullptr) {
            cJSON_AddStringToObject(output_root, "type", type.get());
            cJSON_AddStringToObject(output_root, "host", host.get());
            cJSON_AddNumberToObject(output_root, "port", port);
            cJSON_AddStringToObject(output_root, "username", username.get());
            cJSON_AddStringToObject(output_root, "password", password.get());
            cJSON_AddStringToObject(output_root, "exchange", exchange.get());
            cJSON_AddStringToObject(output_root, "routingkey", routingkey.get());
        }
    }
    // output json
    *ppbody = cJSON_Print(root);
    cJSON_Delete(root);
}

static void request_rtsp_status(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    char* url = (char *)params->arge;
    char** ppbody = (char** )params->argb;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    MasterParams* master = media->GetMaster();
    ConfigParams* config = media->GetConfig();

    // parse http get params
    int offset, limit;
    int n = sscanf(url, "/api/obj/status/rtsp?offset=%d&limit=%d", &offset, &limit);
    if(n != 2) {
        return ;
    }
    // create ack json base
    cJSON* root = cJSON_CreateObject();
    cJSON* data_root =  cJSON_CreateObject();
    cJSON* obj_root =  cJSON_CreateArray();
    cJSON_AddStringToObject(root, "code", "0");
    cJSON_AddStringToObject(root, "msg", "success");
    cJSON_AddItemToObject(root, "data", data_root);
    cJSON_AddItemToObject(data_root, "obj", obj_root);
    // add request obj to json array
    master->m_obj_mtx.lock();
    cJSON_AddNumberToObject(data_root, "total", master->m_obj_vec.size());
    for(size_t i = offset; (int)i < offset + limit && i < master->m_obj_vec.size(); i++) {
        cJSON *fld;
        char alg_name[64] = "none";
        char preview[64] = "none";
        auto obj = master->m_obj_vec[i];
        char* params = obj->params.get();
        auto type = GetStrValFromJson(params, "type");
        if(type != nullptr && strcmp(type.get(), "rtsp") != 0) {
            continue;
        }
        auto name = GetStrValFromJson(params, "name");
        if(name == nullptr) {
            name = std::make_unique<char[]>(32);
            strcpy(name.get(), "");
        }
        auto url = GetStrValFromJson(params, "data", "url");
        int tcp_enable = GetIntValFromJson(params, "data", "tcp_enable");
        if(url == nullptr || tcp_enable < 0) {
            continue;
        }
        auto task_params = obj->GetTask();
        if(task_params != nullptr) {
            auto task_name = GetStrValFromJson(task_params.get(), "task");
            if(task_name != nullptr) {
                strncpy(alg_name, task_name.get(), sizeof(alg_name));
            }
            auto preview_name = GetStrValFromJson(task_params.get(), "params", "preview");
            if(preview_name != nullptr) {
                strncpy(preview, preview_name.get(), sizeof(preview));
            }
        }
        cJSON_AddItemToArray(obj_root, fld = cJSON_CreateObject());
        cJSON_AddStringToObject(fld, "name", name.get());
        cJSON_AddNumberToObject(fld, "id", obj->id);
        cJSON_AddStringToObject(fld, "url", url.get());
        cJSON_AddNumberToObject(fld, "tcp_enable", tcp_enable);
        cJSON_AddStringToObject(fld, "alg", alg_name);
        cJSON_AddStringToObject(fld, "preview", preview);
        cJSON_AddNumberToObject(fld, "status", obj->status);
        // preview url
        const char *ip = "null";
        if(obj->slave != nullptr) {
            if(strlen(obj->slave->internet_ip) > 0) {
                ip = obj->slave->internet_ip;
            }
            else {
                ip = obj->slave->ip;
            }
        }
        char _url[256];
        if(!strncmp(preview, "http-flv", sizeof(preview))) {
            snprintf(_url, sizeof(_url), "http://%s:%d/live?port=1935&app=myapp&stream=stream%d",
                                  ip, config->nginx.http_port, obj->id);
        }
        else {
            snprintf(_url, sizeof(_url), "http://%s:%d/m3u8/stream%d/play.m3u8", 
                                  ip, config->nginx.http_port, obj->id);
        }
        cJSON_AddStringToObject(fld, "previewUrl", _url);
    }
    master->m_obj_mtx.unlock();
    // output json
    *ppbody = cJSON_Print(root);
    cJSON_Delete(root);
}

static void request_slave_status(struct evhttp_request* req, void* arg) {
    request_first_stage;
    CommonParams* params = (CommonParams* )arg;
    char** ppbody = (char** )params->argb;
    Restful* rest = (Restful* )params->argc;
    MediaServer* media = rest->media;
    MasterParams* master = media->GetMaster();

    // create ack json base
    cJSON* root = cJSON_CreateObject();
    cJSON* data_root =  cJSON_CreateObject();
    cJSON* slave_root =  cJSON_CreateArray();
    cJSON_AddStringToObject(root, "code", "0");
    cJSON_AddStringToObject(root, "msg", "success");
    cJSON_AddItemToObject(root, "data", data_root);
    cJSON_AddItemToObject(data_root, "slave", slave_root);
    // add slave to json array
    master->m_slave_mtx.lock();
    cJSON_AddNumberToObject(data_root, "total", master->m_slave_vec.size());
    for(size_t i = 0; i < master->m_slave_vec.size(); i++) {
        cJSON *fld;
        auto slave = master->m_slave_vec[i];
        auto name = GetStrValFromJson(slave->params.get(), "name");
        if(name == nullptr) {
            name = std::make_unique<char[]>(32);
            strcpy(name.get(), "");
        }
        cJSON_AddItemToArray(slave_root, fld = cJSON_CreateObject());
        cJSON_AddStringToObject(fld, "name", name.get());
        cJSON_AddStringToObject(fld, "ip", slave->ip);
        cJSON_AddNumberToObject(fld, "port", slave->rest_port);
        cJSON_AddStringToObject(fld, "internet_ip", slave->internet_ip);
        cJSON_AddNumberToObject(fld, "status", slave->alive);
        cJSON_AddNumberToObject(fld, "load", slave->load.total_load);
        cJSON_AddNumberToObject(fld, "objNum", slave->obj_id_vec.size());
    }
    master->m_slave_mtx.unlock();
    // output json
    *ppbody = cJSON_Print(root);
    cJSON_Delete(root);
}

static UrlMap rest_url_map[] = {
    // HTTP POST
    {"/api/system/login",       request_login},
    {"/api/system/logout",      request_logout},
    {"/api/system/init",        request_system_init},
    {"/api/system/set/output",  request_set_output},
    {"/api/system/slave/add",   request_add_slave},
    {"/api/system/slave/del",   request_del_slave},
    {"/api/obj/add/rtsp",       request_add_rtsp},
    {"/api/obj/del",            request_del_obj},
    {"/api/task/start",         request_start_task},
    {"/api/task/stop",          request_stop_task},
    // HTTP GET
    {"/api/task/support",       request_task_support},
    {"/api/admin/info",         request_admin_info},
    {"/api/system/get/info",    request_system_info},
    {"/api/obj/status/rtsp",    request_rtsp_status},
    {"/api/system/slave/status",request_slave_status},
    {NULL, NULL}
};

MasterRestful::MasterRestful(MediaServer* _media)
    :Restful(_media) {
    ConfigParams* config = media->GetConfig();
    port = config->GetMRestPort();
}

UrlMap* MasterRestful::GetUrl(void) {
    return rest_url_map;
}

const char* MasterRestful::GetType(void) {
    return "master";
}

static void ReadCfg(MasterParams* master) {
    int size = 0;
    const char *filename = "cfg/task.json";
    auto buf = GetArrayBufFromFile(filename, size, "tasks");
    if(buf == nullptr) {
        AppWarn("read %s failed", filename);
        return;
    }
    for(int i = 0; i < size; i ++) {
        auto arrbuf = GetBufFromArray(buf.get(), i);
        if(arrbuf == nullptr) {
            break;
        }
        auto name = GetStrValFromJson(arrbuf.get(), "name");
        auto config = GetStrValFromJson(arrbuf.get(), "config");
        if(name == nullptr || config == nullptr) {
            AppWarn("read name or config failed, %s", filename);
            break;
        }
        std::string _name(name.get());
        master->cfg_task_vec.push_back(_name);
    }
}

MasterParams::MasterParams(MediaServer* _media)
  : media(_media) {
    output = nullptr;
    ReadCfg(this);
}

MasterParams::~MasterParams(void) {
}

static void ResetSlaveObj(auto slave, MasterParams* master) {
    master->m_obj_mtx.lock();
    for(size_t i = 0; i < master->m_obj_vec.size(); i++) {
        auto obj = master->m_obj_vec[i];
        if(obj->slave == slave) {
            obj->slave = nullptr;
        }
    }
    master->m_obj_mtx.unlock();
}

static void UpdateSlaveStatus(auto slave, HttpAck& ack, MasterParams* master) {
    // offline detection
    if(ack.buf == nullptr) {
        slave->offline_cnt ++;
        if(slave->offline_cnt >= 3) {
            if(slave->alive) {
                AppWarn("slave %s:%d is offline", slave->ip, slave->rest_port);
                ResetSlaveObj(slave, master);
            }
            slave->alive = false;
        }
        return;
    }
    if(!slave->alive) {
        // online detection
        AppDebug("slave %s:%d is online", slave->ip, slave->rest_port);
        SlaveSystemInit(slave, master);
    }
    else {
        // restart detection
        int system_init = GetIntValFromJson(ack.buf.get(), "data", "system_init");
        if(system_init == 0) {
            AppWarn("detected slave %s:%d restart", slave->ip, slave->rest_port);
            SlaveSystemInit(slave, master);
            ResetSlaveObj(slave, master);
        }
    }
    slave->alive = true;
    slave->offline_cnt = 0;
}

void MasterParams::SlaveThread(void) {
    char url[256];
    const char* sys_url = "/api/system/status";
    while(media->running) {
        m_slave_mtx.lock();
        for(size_t i = 0; i < m_slave_vec.size(); i++) {
            auto slave = m_slave_vec[i];
            if(slave->offline_cnt > 1 && slave->offline_cnt % 60 != 0) {
                continue;
            }
            HttpAck ack = {nullptr};
            int timeout_sec = slave->alive ? 3 : 1;
            snprintf(url, sizeof(url), "http://%s:%d%s", slave->ip, slave->rest_port, sys_url);
            HttpGet(url, &ack, timeout_sec);
            UpdateSlaveStatus(slave, ack, this);
        }
        m_slave_mtx.unlock();
        sleep(10);
    }
    AppDebug("run ok");
}

static void UpdateObjTask(auto obj, MasterParams* master) {
    if(obj->m_task_vec.size() == 0) {
        return;
    }
    if(obj->slave != nullptr && obj->slave->alive) {
        return;
    }
    if(AssignObjToSlave(obj, master) != true) {
        //app_warning("assign obj %d to slave failed", obj->id);
        obj->status = 0;
        return;
    }

    char buf[512];
    HttpAck ack = {nullptr};
    obj->m_task_mtx.lock();
    for(size_t i = 0; i < obj->m_task_vec.size(); i++) {
        auto task = obj->m_task_vec[i];
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"data\":%s}", obj->id, task.c_str());
        HttpStart(obj, buf, "/api/task/start", &ack, master);
    }
    obj->m_task_mtx.unlock();
}

void MasterParams::ObjThread(void) {
    while(media->running) {
        m_obj_mtx.lock();
        for(size_t i = 0; i < m_obj_vec.size(); i++) {
            auto obj = m_obj_vec[i];
            UpdateObjTask(obj, this);
        }
        m_obj_mtx.unlock();
        sleep(3);
    }
    AppDebug("run ok");
}

void MasterParams::Start(void) {
    DbParams* db = media->GetDB();
    sleep(3); // wait slave restful start ok
    db->DbTraverse("system", media, SystemInitCB);
    db->DbTraverse("slave", media, SlaveCB);
    db->DbTraverse("obj", media, ObjCB);
    std::thread t1(&MasterParams::SlaveThread, this);
    t1.detach();
    std::thread t2(&MasterParams::ObjThread, this);
    t2.detach();
    AppDebug("obj total:%ld", m_obj_vec.size());
    // start master restful
    MasterRestful* rest = new MasterRestful(media);
    rest->Start();
}

