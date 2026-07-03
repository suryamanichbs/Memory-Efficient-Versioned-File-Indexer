#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <cctype>
#include <memory>

// Constraints on Buffer
static const size_t BUFFER_MIN=256;
static const size_t BUFFER_MAX=1024;

// Template that will be used for Storing Key-Value for a Particular Version
template<typename K,typename V>
class KeyValueStore{
    std::unordered_map<K,V> store;
public:
    void increment(const K &key){
        store[key]++;
    }

    V get(const K &key) const{
        typename std::unordered_map<K,V>::const_iterator it=store.find(key);
        if(it!=store.end()) return it->second;
        return V{};
    }

    const std::unordered_map<K,V>& all() const{
        return store;
    }
};

// Class for reading the file using Buffer
class BufferedFileReader{
    std::ifstream file; 
    std::vector<char> buf;
    size_t bufSize;
    size_t readBytes;
    bool reachedEOF; // flag to store End of File Status

public:
    // explicit Constructor
    explicit BufferedFileReader(size_t bufferBytes){

        // Checking for Buffer to be satisfying the constraints
        if(bufferBytes<BUFFER_MIN*1024 || bufferBytes>BUFFER_MAX*1024)
            throw std::invalid_argument("Buffer size must be between 256 KB and 1024 KB.");

        bufSize=bufferBytes;
        readBytes=0;
        reachedEOF=false;
        buf.resize(bufferBytes);
    }

    void open(const std::string &path){
        file.open(path.c_str(),std::ios::binary);
        //Check for file not detected at Path
        if(!file.is_open())
            throw std::runtime_error("Cannot open file: "+path);

        reachedEOF=false;
        readBytes=0;
    }

    bool readChunk(){
        if(reachedEOF)
            return false;

        // Reading the Buffer-Size Chunk
        file.read(&buf[0],(std::streamsize)bufSize);
        readBytes=(size_t)file.gcount();

        if(file.eof()) reachedEOF=true;
        return readBytes>0;
    }

    const char* data() const{
        return &buf[0];
    }

    size_t bytesRead() const{
        return readBytes;
    }

    bool atEOF() const{
        return reachedEOF;
    }

    void close(){
        if(file.is_open()) file.close();
    }

    // Deconstructor
    ~BufferedFileReader(){
        close();
    }
};

class Tokenizer{
    std::string leftover;

public:
    std::vector<std::string> tokenize(const char* data,size_t len,bool lastChunk){
        std::vector<std::string> words;
        std::string cur=leftover;

        for(size_t i=0;i<len;i++){
            unsigned char c=(unsigned char)data[i];

            if(std::isalnum(c)){
                cur+=(char)std::tolower(c);
            }else{
                if(!cur.empty()){
                    words.push_back(cur);
                    cur.clear();
                }
            }
        }

        //check if the chunk is last chunk of the file
        if(lastChunk){
            if(!cur.empty())
                words.push_back(cur);
            
            leftover.clear();
        }else{
            leftover=cur; //handles the condition where buffer splits a word
        }

        return words;
    }
};

class VersionedIndex{
    // use of Key-Value Template as Map for each Version
    std::unordered_map<std::string,KeyValueStore<std::string,int>> versionMap;

public:
    void createVersion(const std::string &name){
        versionMap[name];
    }

    void addWord(const std::string &ver,const std::string &w){
        versionMap.at(ver).increment(w);
    }

    int getCount(const std::string &ver,const std::string &w) const{
        std::unordered_map<std::string,KeyValueStore<std::string,int>>::const_iterator it=versionMap.find(ver);
        if(it==versionMap.end())
            throw std::runtime_error("Version not found: "+ver);

        return it->second.get(w);
    }

    int getCount(const std::string &ver,const std::string &w,int def) const{
        std::unordered_map<std::string,KeyValueStore<std::string,int>>::const_iterator it=versionMap.find(ver);
        if(it==versionMap.end()) return def;
        return it->second.get(w);
    }

    // get top-K words for a particular Version
    std::vector<std::pair<std::string,int> > getTopK(const std::string &ver,int k) const{
        std::unordered_map<std::string,KeyValueStore<std::string,int>>::const_iterator it=versionMap.find(ver);
        if(it==versionMap.end())
            throw std::runtime_error("Version not found: "+ver);

        const std::unordered_map<std::string,int> &mp=it->second.all();

        std::vector<std::pair<std::string,int> > vec;
        std::unordered_map<std::string,int>::const_iterator p;

        for(p=mp.begin();p!=mp.end();p++)
            vec.push_back(*p);

        //sorting using custom comparator
        std::sort(vec.begin(),vec.end(),
        [](const std::pair<std::string,int> &a,const std::pair<std::string,int> &b){
            if(a.second!=b.second) return a.second>b.second;
            return a.first<b.first;
        });

        if(k>(int)vec.size())
            k=vec.size();

        std::vector<std::pair<std::string,int> > ans;
        for(int i=0;i<k;i++)
            ans.push_back(vec[i]);

        return ans;
    }
};

class Query{
protected:
    const VersionedIndex &indexRef;
public:
    Query(const VersionedIndex &idx):indexRef(idx){}
    virtual ~Query(){}

    virtual void execute() const=0;
    virtual std::string typeName() const=0;
};

// WordQuery inherited from Query Class
class WordQuery:public Query{
    std::string ver;
    std::string targetWord;

public:
    WordQuery(const VersionedIndex &idx,const std::string &v,const std::string &w)
        :Query(idx),ver(v),targetWord(w){}

    
    void execute() const{
        int cnt=indexRef.getCount(ver,targetWord);

        std::cout<<"Version: "<<ver<<"\n";
        std::cout<<"Count: "<<cnt<<"\n";
    }

    std::string typeName() const{
        return "word";
    }
};

//Class for DiffQuery
class DiffQuery:public Query{
    std::string verA;
    std::string verB;
    std::string targetWord;

public:
    DiffQuery(const VersionedIndex &idx,const std::string &a,const std::string &b,const std::string &w)
        :Query(idx),verA(a),verB(b),targetWord(w){}

    void execute() const{
        int c1=indexRef.getCount(verA,targetWord,0);
        int c2=indexRef.getCount(verB,targetWord,0);
        int diff=c2-c1;
        std::cout<<"Difference ("<<verB<<" - "<<verA<<"): "<<diff<<"\n";
    }

    std::string typeName() const{
        return "diff";
    }
};

class TopKQuery:public Query{
    std::string ver;
    int topCount;

public:
    TopKQuery(const VersionedIndex &idx,const std::string &v,int k)
        :Query(idx),ver(v),topCount(k){}

    void execute() const{
        std::vector<std::pair<std::string,int> > res=indexRef.getTopK(ver,topCount);

        std::cout<<"Top-"<<topCount<<" words in version "<<ver<<":"<<"\n";

        for(size_t i=0;i<res.size();i++)
            std::cout<<res[i].first<<" "<<res[i].second<<"\n";
    }

    std::string typeName() const{
        return "top";
    }
};

class QueryProcessor{
    VersionedIndex index;

    void indexFile(const std::string &path,const std::string &ver,size_t bufferBytes){
        BufferedFileReader reader(bufferBytes);
        reader.open(path);

        Tokenizer tokenizer;
        index.createVersion(ver);

        while(true){
            if(!reader.readChunk()) break;

            bool last=reader.atEOF();
            std::vector<std::string> tokens=tokenizer.tokenize(reader.data(),reader.bytesRead(),last);

            for(size_t i=0;i<tokens.size();i++)
                index.addWord(ver,tokens[i]);
        }

        reader.close();
    }

    std::string toLower(std::string s){
        for(size_t i=0;i<s.size();i++)
            s[i]=(char)std::tolower((unsigned char)s[i]);
        return s;
    }

public:
    void run(int argc,char* argv[]){
        std::string file,file1,file2;
        std::string ver,ver1,ver2;
        std::string queryType,word;

        size_t bufferKB=512;
        int topK=10;

        for(int i=1;i<argc;i++){
            std::string arg=argv[i];

            if(arg=="--file"){
                if(i+1>=argc) throw std::invalid_argument("Missing value");
                file=argv[++i];
            }
            else if(arg=="--file1"){
                file1=argv[++i];
            }
            else if(arg=="--file2"){
                file2=argv[++i];
            }
            else if(arg=="--version"){
                ver=argv[++i];
            }
            else if(arg=="--version1"){
                ver1=argv[++i];
            }
            else if(arg=="--version2"){
                ver2=argv[++i];
            }
            else if(arg=="--buffer"){
                bufferKB=std::stoul(argv[++i]);
            }
            else if(arg=="--query"){
                queryType=argv[++i];
            }
            else if(arg=="--word"){
                word=argv[++i];
            }
            else if(arg=="--top"){
                topK=std::stoi(argv[++i]);
            }
            else{
                throw std::invalid_argument("Unknown argument: "+arg);
            }
        }

        if(bufferKB<BUFFER_MIN || bufferKB>BUFFER_MAX)
            throw std::invalid_argument("Buffer size must be between 256 and 1024 KB.");

        size_t bufferBytes=bufferKB*1024;

        // Start Time
        std::chrono::high_resolution_clock::time_point t0=
            std::chrono::high_resolution_clock::now();

        if(queryType=="diff"){
            if(file1.empty()||file2.empty()||ver1.empty()||ver2.empty())
                throw std::invalid_argument("Diff query requires files and versions");

            indexFile(file1,ver1,bufferBytes);
            indexFile(file2,ver2,bufferBytes);
        }
        else{
            if(file.empty()||ver.empty())
                throw std::invalid_argument("Query requires file and version");

            indexFile(file,ver,bufferBytes);
        }

        std::unique_ptr<Query> query;

        if(queryType=="word"){
            if(word.empty())
                throw std::invalid_argument("Word query requires word");

            query.reset(new WordQuery(index,ver,toLower(word)));
        }
        else if(queryType=="diff"){
            if(word.empty())
                throw std::invalid_argument("Diff query requires word");

            query.reset(new DiffQuery(index,ver1,ver2,toLower(word)));
        }
        else if(queryType=="top"){
            if(topK<=0)
                throw std::invalid_argument("Invalid top value");

            query.reset(new TopKQuery(index,ver,topK));
        }
        else{
            throw std::invalid_argument("Unknown query type");
        }

        query->execute();

        // End Time
        std::chrono::high_resolution_clock::time_point t1=std::chrono::high_resolution_clock::now();

        // Time Taken
        std::chrono::duration<double> elapsed=t1-t0;

        std::cout<<"Buffer Size (KB): "<<bufferKB<<"\n";
        std::cout<<"Execution Time (s): "<<elapsed.count()<<"\n";
    }
};

int main(int argC,char* argV[]){
    try{
        QueryProcessor qp;
        qp.run(argC,argV);
    }
    catch(const std::exception &e){
        std::cerr<<"Error: "<<e.what()<<"\n";
        return 1;
    }

    return 0;
}