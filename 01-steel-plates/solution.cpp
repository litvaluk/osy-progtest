class SharedPriceList {

public:

    bool ContainsMaterial(unsigned mat) {
      return priceLists.find(mat) != priceLists.end();
    }

    bool MaterialContainsProducer(unsigned mat, AProducer prod) {
      return ContainsMaterial(mat) && knownProducers[mat].find(prod) != knownProducers[mat].end();
    }

    bool Completed(unsigned mat, unsigned numberOfProducers) {
      return ContainsMaterial(mat) && knownProducers[mat].size() == numberOfProducers;
    }

    bool ProducersAreEqual(const CProd& a, const CProd& b){
      return (a.m_W == b.m_W && a.m_H == b.m_H) || (a.m_W == b.m_H && a.m_H == b.m_W);
    }

    APriceList Get(unsigned mat) {
      return priceLists[mat];
    }

    void Add(APriceList pList, AProducer prod) {
      unsigned mat = pList->m_MaterialID;

      if(!ContainsMaterial(mat)) {
        unordered_set<AProducer> tmp;
        tmp.insert(prod);
        knownProducers[mat] = tmp;
        priceLists[mat] = pList;
      }

      else if(!MaterialContainsProducer(mat, prod)) {
        for (auto &n : pList->m_List) {
          bool notFound = true;
          for (auto &o : priceLists[mat]->m_List) {
            if (ProducersAreEqual(o, n)) {
              notFound = false;
              if (n.m_Cost < o.m_Cost) {
                o = n;
              }
            }
          }

          if(notFound) {
            priceLists[mat]->Add(n);
          }
        }

        knownProducers[mat].insert(prod);
      }
    }

private:

    map<unsigned, unordered_set<AProducer>> knownProducers;
    map<unsigned, APriceList> priceLists;

};

class CWeldingCompany {

public:

    static void SeqSolve(APriceList priceList, COrder& order) {
      vector<COrder> orders;
      orders.push_back(order);
      ProgtestSolver(orders, priceList);
      order.m_Cost = orders[0].m_Cost;
    }

    void AddProducer(AProducer prod) {
      producers.push_back(prod);
    }

    void AddCustomer(ACustomer cust) {
      customers.push_back(cust);
    }

    void AddPriceList(AProducer prod, APriceList priceList) {
      unique_lock<mutex> locker(addPriceListMtx);
      sharedPriceList.Add(priceList, prod);
      condPriceList.notify_all();
      locker.unlock();
    }

    bool full() const {
      return buffer.size() >= qMaxSize;
    }

    void CustomerThreadRun(ACustomer c, unsigned id) {
      AOrderList order = c->WaitForDemand();

      while(order.get() != nullptr) {
        unique_lock<mutex> lock(mtxFull);
        condFull.wait(lock, [this] () { return !full(); });
        buffer.push(make_pair(c, order));
        condEmpty.notify_one();
        lock.unlock();
        order = c->WaitForDemand();
      }
    }

    void WorkingThreadRun(unsigned id) {
      while(running || !buffer.empty()) {

        unique_lock<mutex> lock(mtxEmpty);
        condEmpty.wait(lock, [this] () { return !buffer.empty() || !running; });

        if (buffer.empty() && !running) {
          break;
        }

        pair<ACustomer, AOrderList> order = buffer.front();
        buffer.pop();
        condFull.notify_one();
        lock.unlock();

        if (!sharedPriceList.Completed(order.second->m_MaterialID, producers.size())) {
          for (auto& prod : producers) {
            prod->SendPriceList(order.second->m_MaterialID);
          }
        }

        unique_lock<mutex> locker(priceListMtx);
        while (!sharedPriceList.Completed(order.second->m_MaterialID, producers.size())) {
          condPriceList.wait(locker);
        }
        condPriceList.notify_all();
        locker.unlock();

        ProgtestSolver(order.second->m_List, sharedPriceList.Get(order.second->m_MaterialID));
        order.first->Completed(order.second);

      }
    }

    void Start(unsigned thrCount) {
      qMaxSize = thrCount;
      running = true;

      for(unsigned i = 0; i < customers.size(); i++) {
        customerThreads.push_back(thread(&CWeldingCompany::CustomerThreadRun, this, customers[i], i));
      }

      for(unsigned i = 0; i < thrCount; i++) {
        workingThreads.push_back(thread(&CWeldingCompany::WorkingThreadRun, this, i));
      }

    }

    void Stop() {
      for(unsigned i = 0; i < customerThreads.size(); i++) {
        customerThreads[i].join();
      }
      running = false;
      condEmpty.notify_all();

      for(unsigned i = 0; i < workingThreads.size(); i++) {
        workingThreads[i].join();
      }
    }

private:

    bool running;

    queue<pair<ACustomer, AOrderList>> buffer;

    condition_variable condFull;
    condition_variable condEmpty;

    mutex mtxFull;
    mutex mtxEmpty;

    unsigned qMaxSize;

    mutex addPriceListMtx;

    mutex priceListMtx;
    condition_variable condPriceList;

    vector<AProducer> producers;
    vector<ACustomer> customers;

    vector<thread> workingThreads;
    vector<thread> customerThreads;

    SharedPriceList sharedPriceList;
};