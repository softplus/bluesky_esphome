/* Copyright (c) 2023 - John Mueller / MIT license */

#define BLUESKY_USERAGENT "Funky Banana/1.0"
#define BLUESKY_JSON_MAX 2048

// ============================================================
// == Lots of helper functions, the useful ones are on the bottom
// ============================================================

// Filter common unicodes, drop all non-ascii7
// non-latin text gets dropped, but we don't have full unicode fonts, shrug
const std::string _bluesky_filter_text(const std::string str) {
    std::string out;
    out = str;
    // swap out common unicode characters to make it readable
    std::vector<std::string> replaceable{"‘'", "’'", "“\"", "”\""};
    for (auto &swapper : replaceable) {
        std::replace( out.begin(), out.end(), swapper[0], swapper[1]);
    }
    // drop non-ascii7 characters
    out.erase(std::remove_if(out.begin(), out.end(), [](char &ch){
        return ((ch<32) || (ch>126));
    }), out.end());
    return out;
}

// just makes the XML RPC URL
const std::string _bluesky_xrpc_url(std::string service) {
    std::string s = id(bs_server_host);
    s += "xrpc/" + service; // com.atproto.server.createSession
    return s;
}

// Call POST endpoints, supply data as JSON in body
const std::string _bluesky_post(const std::string service, 
        const std::map<std::string, std::string> &payload_map,
        boolean include_auth) {
    WiFiClientSecure wifiClient;
    wifiClient.setInsecure();
    HTTPClient http;
    http.begin(wifiClient, _bluesky_xrpc_url(service).c_str());
    http.setUserAgent(BLUESKY_USERAGENT);
    http.addHeader("Content-Type", "application/json", false, true);
    if (include_auth) {
        http.addHeader("Authorization", id(bs_user_auth).c_str(), false, true);
    }
    DynamicJsonDocument doc(BLUESKY_JSON_MAX);
    JsonObject root = doc.to<JsonObject>();
    for (const auto& n : payload_map) {
        root[n.first] = n.second;
    }
    std::string payload_str;
    serializeJson(doc, payload_str);
    int res = http.sendRequest("POST", payload_str.c_str());
    ESP_LOGD("_bluesky_post:", "HTTP result: %i", res);
    if (res==200) {
        return std::string(http.getString().c_str());
    } else {
        return "";
    }
}

// URL-Encode a string
const std::string _bluesky_urlencode(const std::string str) {
    std::string output;
    char const static hexes[]="0123456789abcdef"; 
    for (const unsigned char &c : str) {
        if ( (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ) {
            output += c;
        } else if (c==' ') {
            output += '+';
        } else {
            output += "%" + std::string(1, hexes[c/16]) + std::string(1, hexes[c%16]); 
        }
    }
    return output;
}

// copied from https://esphome.io/api/json__util_8cpp_source.html
// copy of json::parse_json, with more nesting
void _bluesky_parse_json(const std::string &data, const json::json_parse_t &f) {
   // Here we are allocating 1.5 times the data size,
   // with the heap size minus 2kb to be safe if less than that
   // as we can not have a true dynamic sized document.
   // The excess memory is freed below with `shrinkToFit()`
 #ifdef USE_ESP8266
   const size_t free_heap = ESP.getMaxFreeBlockSize();  // NOLINT(readability-static-accessed-through-instance)
 #elif defined(USE_ESP32)
   const size_t free_heap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
 #elif defined(USE_RP2040)
   const size_t free_heap = rp2040.getFreeHeap();
 #endif
    // NEW ==> for logging
    const char *TAG = "_bluesky_parse_json";  
   bool pass = false;
   size_t request_size = std::min(free_heap, (size_t) (data.size() * 1.5));
   do {
     DynamicJsonDocument json_document(request_size);
     if (json_document.capacity() == 0) {
       ESP_LOGE(TAG, "Could not allocate memory for JSON document! Requested %u bytes, free heap: %u", request_size,
                free_heap);
       return;
     }
     // NEW ==> change nesting limit from 10 to 20
     DeserializationError err = deserializeJson(json_document, data, 
                    DeserializationOption::NestingLimit(20));
     json_document.shrinkToFit();
 
     JsonObject root = json_document.as<JsonObject>();
 
     if (err == DeserializationError::Ok) {
       pass = true;
       f(root);
     } else if (err == DeserializationError::NoMemory) {
       if (request_size * 2 >= free_heap) {
         ESP_LOGE(TAG, "Can not allocate more memory for deserialization. Consider making source string smaller");
         return;
       }
       ESP_LOGV(TAG, "Increasing memory allocation.");
       request_size *= 2;
       continue;
     } else {
       ESP_LOGE(TAG, "JSON parse error: %s", err.c_str());
       return;
     }
   } while (!pass);
 }

// split string into words
const std::vector<std::string> _bluesky_words(const std::string str) {
    std::string word;
    std::string sentence = str;
    std::vector<std::string> out;
    size_t pos = 0;
    while ((pos = sentence.find(" ")) != std::string::npos) {
        word = sentence.substr(0, pos);
        if (!word.empty()) out.push_back(word);
        sentence.erase(0, pos + 1);
    }
    if (!sentence.empty()) out.push_back(sentence);
    return out;
}

// generic GET request for 
const std::string _bluesky_get(const std::string service, 
        const std::map<std::string, std::string> &query_map,
        boolean include_auth) {
    WiFiClientSecure wifiClient;
    wifiClient.setInsecure();
    HTTPClient http;
    std::string url;
    for (const auto& n : query_map) {
        if (url=="") url += "?"; else url += "&";
        url += _bluesky_urlencode(n.first) + "=" + _bluesky_urlencode(n.second);
    }
    url = _bluesky_xrpc_url(service) + url;
    ESP_LOGD("_bluesky_get:", "URL: %s", url.c_str());
    http.begin(wifiClient, url.c_str());
    http.setUserAgent(BLUESKY_USERAGENT);
    if (include_auth) {
        http.addHeader("Authorization", id(bs_user_auth).c_str(), false, true);
    }
    int res = http.sendRequest("GET");
    ESP_LOGD("_bluesky_get:", "HTTP result: %i", res);
    if (res==200) {
        return std::string(http.getString().c_str());
    } else {
        return "";
    }
}

// ============================================================
// == Stuff here is for calling
// ============================================================

// log in with your account
const boolean bluesky_login(std::string username, std::string password) {
    id(bs_post_text) = "Logging in";
    std::map<std::string, std::string> m;
    m["identifier"] = username;
    m["password"] = password;
    id(bs_user_auth) = "";
    std::string res = _bluesky_post("com.atproto.server.createSession", m, false);
    if (res!="") {
        json::parse_json(res, [](JsonObject root) {
            const std::string my_did = root["did"];
            const std::string my_handle = root["handle"];
            const std::string my_auth = root["accessJwt"];
            id(bs_user_did) = my_did; // store
            id(bs_user_handle) = my_handle;
            id(bs_user_auth) = "Bearer " + my_auth; // store auth with Bearer text
            ESP_LOGD("bluesky_login:", "Received DID: '%s', handle: '%s'", 
                        my_did.c_str(), my_handle.c_str());
        });
        id(bs_logged_in) = (id(bs_user_auth)!="");
    } else { // login failed, clear settings
        ESP_LOGD("bluesky_login:", "No data received.");
        id(bs_user_did) = "";
        id(bs_user_handle) = "";
        id(bs_user_auth) = "";
        id(bs_logged_in) = false;
    }
    if (id(bs_logged_in)) id(bs_post_text) = "Logged in.";
    else id(bs_post_text) = "Login failed.";
    return id(bs_logged_in);
}

// get notification count
const boolean bluesky_check_unread() {
    if (!id(bs_logged_in)) return false;
    std::map<std::string, std::string> q;
    std::string res = _bluesky_get("app.bsky.notification.getUnreadCount", q, true);
    if (res!="") {
        _bluesky_parse_json(res, [](JsonObject root) {
            const int count = root["count"];
            id(bs_has_unread) = count;
            ESP_LOGI("bluesky_check_unread:", "Count: %i", count);
        });
        return true;
    } else {
        ESP_LOGD("bluesky_check_unread:", "No data received.");
    }
    return false;
}

// get top popular posts
std::map<std::string, std::string> bluesky_get_pops(boolean filter_text) {
    std::map<std::string, std::string> ret;
    if (!id(bs_logged_in)) { ret["error"] = "login"; return ret; }
    std::map<std::string, std::string> q;
    q["limit"] = "1";
    std::string res = _bluesky_get("app.bsky.unspecced.getPopular", q, true);
    if (res!="") {
        ESP_LOGD("bluesky_get_pops:", res.c_str());
        ret["handle"] = ""; ret["name"] = ""; ret["date"] = ""; ret["text"] = "";
        //json::parse_json(res, [&ret](JsonObject root) {
        _bluesky_parse_json(res, [&ret](JsonObject root) {
            const std::string shand = root["feed"][0]["post"]["author"]["handle"];
            const std::string sname = root["feed"][0]["post"]["author"]["displayName"];
            const std::string sdate = root["feed"][0]["post"]["record"]["createdAt"];
            const std::string stext = root["feed"][0]["post"]["record"]["text"];
            ret["handle"] = shand;
            ret["name"] = sname;
            ret["date"] = sdate;
            ret["text"] = stext;
            ESP_LOGI("bluesky_get_pops:", "Text: '%s'", stext.c_str());
        });
        if (filter_text) {
            for (const auto& n : ret) ret[n.first] = _bluesky_filter_text(n.second);
        }
        std::vector<std::string> v = _bluesky_words(ret["text"]);
        id(bs_disp_words) = v;
        ret["error"] = "";
        id(bs_post_handle) = ret["handle"];
        id(bs_post_name) = ret["name"];
        id(bs_post_date) = ret["date"];
        id(bs_post_text) = ret["text"];
    } else {
        ret["error"] = "no data";
        ESP_LOGD("bluesky_get_pops:", "No data received.");
    }
    return ret;
}

// call this once to set the server
void bluesky_set_server(std::string hostname) {
    std::string host = hostname;
    if (host.rfind("https://", 0) != 0) host = "https://" + host;
    if (host.back()!='/') host = host + "/";
    id(bs_server_host) = host;
}

