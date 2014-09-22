# include <string>
# include <deque>
# include <ts/ts.h>

class DomainNameTree {
public:
  
  class DomainNameNode {
  public:
    DomainNameNode() : order(-1), payload(NULL), parent(NULL), is_wild(false) 
    {
    }
    DomainNameNode(std::string key, void *payload, int order, bool is_wild) : key(key), order(order), payload(payload), parent(NULL), is_wild(is_wild)  
    {
    }
    DomainNameNode *match(std::string value);
    ~DomainNameNode() {
      std::deque<DomainNameNode *>::iterator iter = children.begin();
      for (; iter != children.end(); iter++) {
        delete *iter;
      }
    }
    // return true if comparable.  Return type of compare in relative parameter
    // 0 if eq.  < 0 if node key is broader.  > 0 if parameter key is broader
    bool compare(std::string key, int &relative);
    // The wildcard is pruned out of the key
    bool prunedCompare(std::string key, int &relative, bool is_wild);
    std::string key;	// The string trailing the * (if any)
    int order;		// Track insert order for conflict resolution
    void *payload;
    std::deque<DomainNameNode *> children;
    DomainNameNode *parent;
    bool is_wild;
  private:
  };

  DomainNameTree() {
    root = new DomainNameNode();
    root->key = "";
    root->order = 0x7FFFFFFF;
    root->is_wild = true;
    tree_mutex = TSMutexCreate(); 
  }
  ~DomainNameTree() {
    if (root != NULL) { delete root; }
  }
  DomainNameNode *findBestMatch(std::string key)  {
    TSMutexLock(this->tree_mutex);
    DomainNameNode *retval = this->find(key, true);
    TSMutexUnlock(this->tree_mutex);
    return retval;
  } 
  DomainNameNode *findFirstMatch(std::string key) {
    TSMutexLock(this->tree_mutex);
    DomainNameNode *retval = this->find(key, false);
    TSMutexUnlock(this->tree_mutex);
    return retval;
  }
  DomainNameNode *find(std::string key, bool best_match);
  DomainNameNode *insert(std::string key, void *payload, int order);

private:
  DomainNameNode *root;
  TSMutex tree_mutex;
};

