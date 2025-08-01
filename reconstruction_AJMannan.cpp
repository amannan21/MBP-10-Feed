#include <algorithm>   // std::max
#include <cassert>
#include <charconv>
#include <fstream>      // std::ifstream, std::ofstream
#include <iomanip>      // std::setw, std::setfill
#include <iostream>     // std::cerr
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
using namespace std;

/* ───── type aliases ───── */
using OrderId = long long;
using Size    = long long;
using PriceIx = long long;          // integer price key (micro-dollars)

/* ───── helpers ───── */
static PriceIx price_to_key(string_view s) {
    // convert "5.51" -> 5510000 (micro-dollars, 1e-6)
    if (s.empty()) return 0;
    try {
        double d = stod(string(s));
        return static_cast<PriceIx>(llround(d * 1'000'000));
    } catch (...) {
        return 0;
    }
}

static string_view trim(string_view v) {
    while(!v.empty() && v.back()=='\r') v.remove_suffix(1);
    return v;
}
static vector<string_view> split(string_view s) {
    vector<string_view> out;
    size_t st=0;
    for(size_t i=0;i<=s.size();++i)
        if(i==s.size()||s[i]==','){ out.emplace_back(s.data()+st,i-st); st=i+1; }
    return out;
}

/* ───── book structs ───── */
struct Order {
    char  side;          // 'B' or 'A'
    PriceIx key;         // price key
    string price_str;    // original display
    Size  size;
};

struct Level { Size size=0; int cnt=0; string price_str; };

struct Book {
    unordered_map<OrderId,Order> by_id;
    map<PriceIx,Level,greater<>> bids;
    map<PriceIx,Level,less<>>    asks;

    void add(OrderId id,char side,PriceIx key,string price,Size sz){
        by_id[id] = {side, key, std::move(price), sz};
        auto &lvl=(side=='B'?bids[key]:asks[key]);
        lvl.size+=sz; lvl.cnt+=1; if(lvl.price_str.empty()) lvl.price_str=by_id[id].price_str;
    }
    void remove_level(map<PriceIx,Level,greater<>> &m,PriceIx k){
        auto it=m.find(k); if(it!=m.end()&&it->second.size==0) m.erase(it);
    }
void cancel(OrderId id, Size delta) {
    auto it = by_id.find(id);
    if (it == by_id.end()) return;

    Order &o = it->second;
    delta = std::min(delta, o.size);
    auto &lvl = (o.side == 'B' ? bids[o.key] : asks[o.key]);
    lvl.size -= delta;
    
    // Only decrement count if we're removing the entire order
    if (delta == o.size) {
        lvl.cnt -= 1;
    }

    if (lvl.size == 0) {                 // remove empty price level
        if (o.side == 'B')
            bids.erase(o.key);
        else
            asks.erase(o.key);
    }
    
    // Update the order size or remove it
    o.size -= delta;
    if (o.size == 0) {
        by_id.erase(it);
    }
}
    void modify(OrderId id,PriceIx k,string price,Size sz,char side){
        auto it = by_id.find(id);
        if (it != by_id.end()) {
            Size old_size = it->second.size;
            cancel(id, old_size);
        }
        add(id,side,k,price,sz);
    }
    void clear(){ by_id.clear(); bids.clear(); asks.clear(); }

    /* dump 60 MBP fields (px,sz,ct x10 bids then asks) */
    void dump_levels(ostream &os){
        auto dump=[&](auto& tree){
            int lvl=0; auto it=tree.begin();
            for(;lvl<10;++lvl){
                if(it!=tree.end()){
                    os<<','<<it->second.price_str
                       <<','<<it->second.size
                       <<','<<it->second.cnt;
                    ++it;
                } else os<<",,"<<",0,0";
            }
        };
        dump(bids); dump(asks);
    }
};

/* ───── main ───── */
int main(int argc,char**argv){
    if(argc!=2){ cerr<<"Usage: "<<argv[0]<<" mbo.csv\n"; return 1;}
    ifstream in(argv[1]); if(!in){ cerr<<"Cannot open "<<argv[1]<<"\n"; return 1;}

    ofstream out("mbp_new.csv");
    /* write header */
    // out<<",ts_recv,ts_event,rtype,publisher_id,instrument_id,action,side,depth,"
    //    <<"price,size,flags,ts_in_delta,sequence";
    for(int i=0;i<10;i++){
        out<<",bid_px_"<<setw(2)<<setfill('0')<<i
           <<",bid_sz_"<<setw(2)<<i
           <<",bid_ct_"<<setw(2)<<i
           <<",ask_px_"<<setw(2)<<i
           <<",ask_sz_"<<setw(2)<<i
           <<",ask_ct_"<<setw(2)<<i;
    }
    out<<",symbol,order_id\n";
    out<<setfill(' ');

    string header; getline(in,header);        // skip original header
    vector<string_view> h=split(header);
    auto col=[&](string name){ for(int i=0;i<(int)h.size();++i) if(h[i]==name) return i; return -1;};

    int I_ts_recv=col("ts_recv"), I_ts_event=col("ts_event"), I_rtype=col("rtype"),
        I_pub=col("publisher_id"), I_instr=col("instrument_id"), I_action=col("action"),
        I_side=col("side"), I_price=col("price"), I_size=col("size"),
        I_flags=col("flags"), I_delta=col("ts_in_delta"), I_seq=col("sequence"),
        I_symbol=col("symbol"), I_oid=col("order_id"), I_channel=col("channel_id");

    // Check that all required columns were found
    if (I_ts_recv == -1 || I_ts_event == -1 || I_action == -1 || I_side == -1 || 
        I_price == -1 || I_size == -1 || I_oid == -1) {
        cerr << "Missing required columns in CSV header\n";
        return 1;
    }

    Book book; long row=0;
    string line;
    while(getline(in,line)){
        auto f=split(trim(line));
        if (f.size() < 15) continue; // Skip malformed lines
        
        // Additional safety checks for array bounds
        int max_idx = std::max({I_ts_recv, I_ts_event, I_rtype, I_pub, I_instr, I_action, 
                               I_side, I_price, I_size, I_flags, I_delta, I_seq, I_symbol, I_oid, I_channel});
        if (max_idx >= (int)f.size()) continue; // Skip if any required column is out of bounds
        
        /* echo metadata */
        out<<row++<<','<<f[I_ts_recv]<<','<<f[I_ts_event]<<','<<f[I_rtype]<<','<<f[I_pub]
           <<','<<f[I_instr]<<','<<f[I_action]<<','<<f[I_side]<<','<<(I_channel >= 0 ? f[I_channel] : "")
           <<','<<f[I_price]<<','<<f[I_size]<<','<<f[I_flags]
           <<','<<f[I_delta]<<','<<f[I_seq];

        /* update book */
        char act=f[I_action].empty()?'N':f[I_action][0];
        char side=f[I_side].empty()?'N':f[I_side][0];
        OrderId oid = 0;
        if (!f[I_oid].empty()) {
            try {
                oid = stoll(string(f[I_oid]));
            } catch (...) {
                oid = 0;
            }
        }
        string price_str = string(f[I_price]);
        PriceIx key = price_str.empty()?0:price_to_key(price_str);
        Size sz = 0;
        if (!f[I_size].empty()) {
            try {
                sz = stoll(string(f[I_size]));
            } catch (...) {
                sz = 0;
            }
        }

        switch(act){
            case 'A': book.add(oid,side,key,price_str,sz); break;
            case 'C': book.cancel(oid,sz);                 break;
            case 'M': book.modify(oid,key,price_str,sz,side); break;
            case 'R': book.clear();                       break;
            default: break; // T, F, N
        }

        /* dump MBP levels */
        book.dump_levels(out);
        out<<','<<f[I_symbol]<<','<<f[I_oid]<<'\n';
    }
    return 0;
}
