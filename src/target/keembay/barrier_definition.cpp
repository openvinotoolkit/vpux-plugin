#include "include/mcm/target/keembay/barrier_definition.hpp"

mv::Barrier::Barrier(int group, int index) :
group_(group),
index_(index),
numProducers_(0),
numConsumers_(0)
{

}

mv::Barrier::Barrier(int group, int index, std::unordered_set<std::string>& producers, std::unordered_set<std::string>& consumers) :
group_(group),
index_(index),
numProducers_(producers.size()),
numConsumers_(consumers.size()),
producers_(producers),
consumers_(consumers)
{

}

mv::Barrier::Barrier(std::unordered_set<std::string>& producers, std::unordered_set<std::string>& consumers) :
group_(-1),
index_(-1),
numProducers_(producers.size()),
numConsumers_(consumers.size()),
producers_(producers),
consumers_(consumers)
{

}

int mv::Barrier::getNumProducers() const
{
    return numProducers_;
}

int mv::Barrier::getNumConsumers() const
{
    return numConsumers_;
}

void mv::Barrier::setGroup(int group)
{
    group_ = group;
}

void mv::Barrier::setIndex(int index)
{
    index_ = index;
}

int mv::Barrier::getIndex() const
{
    return index_;
}

int mv::Barrier::getGroup() const
{
    return group_;
}

void mv::Barrier::addProducer(const std::string& producer)
{
    if (!producers_.count(producer))
    {
        producers_.insert(producer);
        numProducers_++;
    }
}

void mv::Barrier::addConsumer(const std::string& consumer)
{
    if (!consumers_.count(consumer))
    {
        consumers_.insert(consumer);
        numConsumers_++;
    }
}

void mv::Barrier::removeProducer(const std::string& producer)
{
    auto it = producers_.find(producer);

    if (it == producers_.end())
        return;

    producers_.erase(it);
    numProducers_--;
}

void mv::Barrier::removeConsumer(const std::string& consumer)
{
    auto it = consumers_.find(consumer);

    if (it == consumers_.end())
        return;

    consumers_.erase(it);
    numConsumers_--;
}

void mv::Barrier::clear()
{
    numProducers_ = 0;
    numConsumers_ = 0;
    group_ = -1;
    index_ = -1;
    producers_.clear();
    consumers_.clear();
}

bool mv::Barrier::hasProducers()
{
    return !producers_.empty();
}

bool mv::Barrier::hasConsumers()
{
    return !consumers_.empty();
}

std::unordered_set<std::string> mv::Barrier::getProducers() const
{
    return producers_;
}

std::unordered_set<std::string> mv::Barrier::getConsumers() const
{
    return consumers_;
}

std::string mv::Barrier::toString() const
{
    std::string output = "";

    output += "group: " + std::to_string(group_) + " | ";
    output += "index: " + std::to_string(index_) + " | ";
    output += "nProd: " + std::to_string(numProducers_) + " | ";
    output += "nCons: " + std::to_string(numConsumers_) + "\n";

    return output;
}

std::string mv::Barrier::getLogID() const
{
    return "Barrier:" + toString();
}