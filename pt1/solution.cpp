#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <compare>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
mutex mtxMinSolver;
mutex mtxCntSolver;
mutex mtxGlobalBuffer;
condition_variable cvGlobalBuffer;
atomic_size_t solvedMegaCount = 0;
atomic_size_t threadCnt = 0;
atomic_size_t packId = 0;

string dashes(80, '-');
string dashesSmall(10, '-');

struct problemPack {
    atomic_size_t id; //debug only
    AProblemPack pack;
    ACompany from;
    atomic_size_t toSolve;
};

struct companyWrapper {
    mutex mtxCompany;
    ACompany company;
    condition_variable cvOut;
    thread threadIn, threadOut;
    queue<shared_ptr<problemPack>> companyProblems;
};

struct solverWrapper {
    unordered_map<shared_ptr<problemPack>, size_t> fromCnt, fromMin;
    AProgtestSolver minSolver = createProgtestMinSolver();
    AProgtestSolver cntSolver = createProgtestCntSolver();

    void newMinSolver() { minSolver = createProgtestMinSolver(); }

    void newCntSolver() { cntSolver = createProgtestCntSolver(); }
};


class COptimizer {
public:
    static bool usingProgtestSolver(void) {
        return true;
    }

    static void checkAlgorithmMin(APolygon p) {
        // dummy implementation if usingProgtestSolver() returns true
    }

    static void checkAlgorithmCnt(APolygon p) {
        // dummy implementation if usingProgtestSolver() returns true
    }

    void solverParty(shared_ptr<problemPack> problem, bool cnt) {
        vector<shared_ptr<CPolygon>> polygonProblems;
        AProgtestSolver *thisSolver;
        unordered_map<shared_ptr<problemPack>, size_t> *from;

        if (cnt) {
            polygonProblems = problem->pack->m_ProblemsCnt;
            thisSolver = &solver.cntSolver;
            from = &solver.fromCnt;
        } else {
            polygonProblems = problem->pack->m_ProblemsMin;
            thisSolver = &solver.minSolver;
            from = &solver.fromMin;
        }

        unique_lock<mutex> m(cnt ? mtxCntSolver : mtxMinSolver);
        for (auto &polygon: polygonProblems) {
            if ((*thisSolver)->hasFreeCapacity()) {
                (*from)[problem]++;
                (*thisSolver)->addPolygon(polygon);
            } else {
                auto localFrom = *from;
                auto fullSolver = *thisSolver;

                from->clear();
                cnt ? solver.newCntSolver() : solver.newMinSolver();
                m.unlock();

                auto solvedCount = fullSolver->solve();
                solvedMegaCount += solvedCount;
                printf("%s solved %ld\n", cnt ? "cnt" : "min", solvedCount);

                for (auto &[pack, count]: localFrom) {
                    pack->toSolve -= count;
                    if (pack->toSolve == 0) companies[pack->from].cvOut.notify_one();
                }
                m.lock();
                thisSolver = cnt ? &solver.cntSolver : &solver.minSolver;
                from = cnt ? &solver.fromCnt : &solver.fromMin;
                (*from)[problem] = 1;
                (*thisSolver)->addPolygon(polygon);
            }
        }

        if (!(*thisSolver)->hasFreeCapacity()) {
            auto localFrom = *from;
            auto fullSolver = *thisSolver;
            from->clear();
            cnt ? solver.newCntSolver() : solver.newMinSolver();

            m.unlock();
            auto solvedCount = fullSolver->solve();
            solvedMegaCount += solvedCount;

            for (auto &[pack, count]: localFrom) {
                pack->toSolve -= count;
                if (pack->toSolve == 0) companies[pack->from].cvOut.notify_one();
            }

        }
    }

    void work() {
        while (true) {
            unique_lock<mutex> m(mtxGlobalBuffer);
            cvGlobalBuffer.wait(m, [this] { return !problems.empty(); });

            auto problem = problems.front();
            if (!problem->pack || !problem) break; // buffer mutex unlocked when broken out of scope

            problems.pop();

            m.unlock();
            cvGlobalBuffer.notify_one();

            solverParty(problem, true);
            solverParty(problem, false);
        }
        cvGlobalBuffer.notify_all();


        threadCnt--;
        if (threadCnt == 0) {
            solver.cntSolver->solve();
            for (auto &[pack, count]: solver.fromCnt) {
                pack->toSolve -= count;
                if (pack->toSolve == 0) companies[pack->from].cvOut.notify_one();
            }

            solver.minSolver->solve();
            for (auto &[pack, count]: solver.fromMin) {
                pack->toSolve -= count;
                if (pack->toSolve == 0) companies[pack->from].cvOut.notify_one();
            }

        }
    }


    void incoming(companyWrapper &company) {
        while (true) {
            auto pack = company.company->waitForPack();
            if (!pack) break;
            auto problem = make_shared<problemPack>(packId++, pack, company.company,
                                                    pack->m_ProblemsMin.size() + pack->m_ProblemsCnt.size());
            unique_lock<mutex> m(company.mtxCompany);
            company.companyProblems.push(problem);
            m.unlock();

            unique_lock<mutex> m1(mtxGlobalBuffer);
            problems.push(problem);
            cvGlobalBuffer.notify_one();
        }

        unique_lock<mutex> m(company.mtxCompany);
        company.companyProblems.push(make_shared<problemPack>(packId++, nullptr, company.company, 0));
    }

    void outgoing(companyWrapper &company) {
        while (true) {
            unique_lock<mutex> m(company.mtxCompany);
            auto companyProblems = &company.companyProblems;
            company.cvOut.wait(m, [&companyProblems] {
                return !companyProblems->empty() && companyProblems->front()->toSolve == 0;
            });

            if (!companyProblems->front()->pack) break;

            company.company->solvedPack(companyProblems->front()->pack);
            companyProblems->pop();
        }
    }

    void start(int threadCount) {
        threadCnt = threadCount;
        for (int i = 0; i < threadCount; ++i)
            workThreads.emplace_back(&COptimizer::work, this);

        for (auto &[company, wrapper]: companies) {
            wrapper.company = company;
            wrapper.threadIn = thread(&COptimizer::incoming, this, ref(wrapper));
            wrapper.threadOut = thread(&COptimizer::outgoing, this, ref(wrapper));
        }
    };

    void stop(void) {
        for (auto &[company, threads]: companies)
            threads.threadIn.join();

        unique_lock<mutex> m2(mtxGlobalBuffer);
        problems.push(make_shared<problemPack>(--packId, nullptr, companies.begin()->first, 0));
        m2.unlock();
        cvGlobalBuffer.notify_all();

        for (auto &t: workThreads)
            t.join();

        for (auto &[company, threads]: companies)
            threads.threadOut.join();
    };

    void addCompany(ACompany company) {
        companies[std::move(company)];
    };

private:
    solverWrapper solver;
    vector<thread> workThreads;
    queue<shared_ptr<problemPack>> problems;
    unordered_map<ACompany, companyWrapper> companies;
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main(void) {
    COptimizer optimizer;
    ACompanyTest company = std::make_shared<CCompanyTest>();
    optimizer.addCompany(company);

    int numOfCompanies = 4;
    vector<shared_ptr<CCompanyTest>> cmps;
    for (int i = 0; i < numOfCompanies; ++i)
        cmps.push_back(make_shared<CCompanyTest>());
    for (int i = 0; i < numOfCompanies; ++i)
        optimizer.addCompany(cmps[i]);

    optimizer.start(20);
    optimizer.stop();

    if (!company->allProcessed())
        throw std::logic_error("(some) problems were not correctly processsed");

    for (int i = 0; i < numOfCompanies; ++i)
        if (!cmps[i]->allProcessed())
            throw std::logic_error("(some) problems were not correctly processsed");

    cout << "mega solved: " << solvedMegaCount << endl;
    cout << dashes << "fucking fuck fuck fuck shit done" << dashes << endl;
    return 0;
}

#endif /* __PROGTEST__ */
