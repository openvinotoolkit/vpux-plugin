#include "include/mcm/pass/pass_manager.hpp"

mv::ExecutionError::ExecutionError(const std::string& whatArg) :
std::runtime_error(whatArg)
{

}

mv::PassManager::PassManager() :
ready_(false),
completed_(false),
running_(false),
model_(nullptr),
currentStage_(passFlow_.cend()),
currentPass_(adaptPassQueue_.end())
{

}

bool mv::PassManager::initialize(ComputationModel &model, const TargetDescriptor& targetDescriptor, const mv::json::Object& compDescriptor)
{
    
    if (running_)
        return false;

    reset();
    
    targetDescriptor_ = targetDescriptor;
    adaptPassQueue_ = targetDescriptor_.adaptPasses();
    optPassQueue_ = targetDescriptor_.optPasses();
    finalPassQueue_ = targetDescriptor_.finalPasses();
    serialPassQueue_ = targetDescriptor_.serialPasses();
    validPassQueue_ = targetDescriptor_.validPasses();

    auto checkQueue = [this](const std::vector<std::string>& queue, PassGenre genre)
    {
        for (std::size_t i = 0; i < queue.size(); ++i)
        {

            pass::PassEntry *passPtr = pass::PassRegistry::instance().find(queue[i]);
            if (passPtr == nullptr)
            {
                reset();
                return false;
            }

            auto passGenres = passPtr->getGenre();
            if (passGenres.find(genre) == passGenres.end())
            {
                reset();
                return false;
            }

        }

        return true;

    };

    if (!checkQueue(adaptPassQueue_, PassGenre::Adaptation))
        return false;

    if (!checkQueue(optPassQueue_, PassGenre::Optimization))
        return false;

    if (!checkQueue(finalPassQueue_, PassGenre::Finalization))
        return false;

    if (!checkQueue(serialPassQueue_, PassGenre::Serialization))
        return false;

    if (!checkQueue(validPassQueue_, PassGenre::Validation))
        return false;

    if (!model.isValid())
    {
        reset();
        return false;
    }

    model_ = &model;
    ready_ = true;
    completed_ = false;
    running_ = false;
    currentStage_ = passFlow_.cbegin();
    currentPass_ = currentStage_->second.begin();
    compDescriptor_ = compDescriptor;
    return true;

}

bool mv::PassManager::enablePass(PassGenre stage, const std::string& pass, int pos)
{
    if (!ready() || running_)
        return false;

    pass::PassEntry *passPtr = pass::PassRegistry::instance().find(pass);
    if (passPtr == nullptr)
        return false;

    auto passGenres = passPtr->getGenre();
    if (passGenres.find(stage) == passGenres.end())
        return false;
    
    std::vector<std::string>* queuePtr = nullptr;

    switch (stage)
    {

        case PassGenre::Adaptation:
            queuePtr = &adaptPassQueue_;
            break;

        case PassGenre::Optimization:
            queuePtr = &optPassQueue_;
            break;

        case PassGenre::Finalization:
            queuePtr = &finalPassQueue_;
            break;
        
        case PassGenre::Serialization:
            queuePtr = &serialPassQueue_;
            break;

        case PassGenre::Validation:
            queuePtr = &validPassQueue_;
            break;
        
    }

    if (pos > (int)queuePtr->size() || pos < -1)
        return false;

    if (pos == (int)queuePtr->size() || pos == -1)
    {
        queuePtr->push_back(pass);
        return true;
    }

    auto result = queuePtr->insert(queuePtr->begin() + pos, pass);

    if (result != queuePtr->end())
        return true;

    return false;

}

bool mv::PassManager::disablePass(PassGenre stage, const std::string& pass)
{

    if (!ready() || running_)
        return false;

    std::vector<std::string>* queuePtr = nullptr;

    switch (stage)
    {

        case PassGenre::Adaptation:
            queuePtr = &adaptPassQueue_;
            break;

        case PassGenre::Optimization:
            queuePtr = &optPassQueue_;
            break;

        case PassGenre::Finalization:
            queuePtr = &finalPassQueue_;
            break;
        
        case PassGenre::Serialization:
            queuePtr = &serialPassQueue_;
            break;

        case PassGenre::Validation:
            queuePtr = &validPassQueue_;
            break;
        
    }

    auto passIt = std::find(queuePtr->begin(), queuePtr->end(), pass);
    bool flag = false;

    while (passIt != queuePtr->end())
    {
        flag = true;
        queuePtr->erase(passIt);
        passIt = std::find(queuePtr->begin(), queuePtr->end(), pass);

    }

    return flag;

}

bool mv::PassManager::disablePass(PassGenre stage)
{

    if (!ready() || running_)
        return false;

    std::vector<std::string>* queuePtr = nullptr;

    switch (stage)
    {

        case PassGenre::Adaptation:
            queuePtr = &adaptPassQueue_;
            break;

        case PassGenre::Optimization:
            queuePtr = &optPassQueue_;
            break;

        case PassGenre::Finalization:
            queuePtr = &finalPassQueue_;
            break;
        
        case PassGenre::Serialization:
            queuePtr = &serialPassQueue_;
            break;

        case PassGenre::Validation:
            queuePtr = &validPassQueue_;
            break;
        
    }

    queuePtr->clear();

    return true;

}

bool mv::PassManager::disablePass()
{

    if (!ready() || running_)
        return false;

    adaptPassQueue_.clear();
    optPassQueue_.clear();
    finalPassQueue_.clear();
    serialPassQueue_.clear();
    validPassQueue_.clear();

    return true;

}

void mv::PassManager::reset()
{

    model_ = nullptr;
    ready_ = false;
    completed_ = false;
    running_ = false;
    adaptPassQueue_.clear();
    optPassQueue_.clear();
    finalPassQueue_.clear();
    serialPassQueue_.clear();
    validPassQueue_.clear();
    currentStage_ = passFlow_.cbegin();
    currentPass_ = currentStage_->second.end();
    targetDescriptor_ = TargetDescriptor();
    compDescriptor_ = json::Object();

}

bool mv::PassManager::ready() const
{
    return model_ && ready_;
}

bool mv::PassManager::completed() const
{
    return ready() && !running_ && completed_;
}

std::pair<std::string, mv::PassGenre> mv::PassManager::step()
{

    if (!running_)
    {
        if (!validDescriptors())
            throw ExecutionError("Invalid descriptor");
        running_ = true;
        currentStage_ = passFlow_.cbegin();
        currentPass_ = currentStage_->second.begin();
        buffer_.clear();
    }

    if (!completed_ && currentStage_ != passFlow_.end())
    {

        while (currentPass_ == currentStage_->second.end())
        {
            ++currentStage_;

            if (currentStage_->first == PassGenre::Validation)
            {
                while (currentStage_ != passFlow_.begin() && (currentStage_ - 1)->second.size() == 0 && currentStage_ != passFlow_.end())
                    ++currentStage_;
            }

            if (currentStage_ == passFlow_.end())
            {
                completed_ = true;
                running_ = false;
                return std::pair<std::string, mv::PassGenre>("", PassGenre::Serialization);
            }

            currentPass_ = currentStage_->second.begin();
        }
            
        auto passPtr = pass::PassRegistry::instance().find(*currentPass_);

        if (passPtr->getName() == "GenerateDot")
        {
            if (buffer_.empty())
            {
                buffer_ = compDescriptor_["GenerateDot"]["output"].get<std::string>();
                buffer_ = buffer_.substr(0, buffer_.find("."));
            }

            std::string postFix = "";

            if (currentStage_ != passFlow_.begin())
            {
                auto previousStage = currentStage_ - 1;
                switch (previousStage->first)
                {
                    case PassGenre::Adaptation:
                        postFix = "_adapt";
                        break;

                    case PassGenre::Optimization:
                        postFix = "_opt";
                        break;

                    case PassGenre::Finalization:
                        postFix = "_final";
                        break;

                    default:
                        postFix = "_unknown";

                }
            }

            compDescriptor_["GenerateDot"]["output"] = buffer_ + postFix + ".dot";

        }

        passPtr->run(*model_, targetDescriptor_, compDescriptor_);
        std::pair<std::string, mv::PassGenre> result({*currentPass_, currentStage_->first});
        currentPass_++;
        return result;

    }

    return std::pair<std::string, mv::PassGenre>("", PassGenre::Adaptation);

}

std::size_t mv::PassManager::scheduledPassesCount(PassGenre stage) const
{
    return scheduledPasses(stage).size();   
}

const std::vector<std::string>& mv::PassManager::scheduledPasses(PassGenre stage) const
{
    switch (stage)
    {
        case PassGenre::Adaptation:
            return adaptPassQueue_;

        case PassGenre::Optimization:
            return optPassQueue_;

        case PassGenre::Finalization:
            return finalPassQueue_;
        
        case PassGenre::Serialization:
            return serialPassQueue_;

        case PassGenre::Validation:
            return validPassQueue_;
    }

    return adaptPassQueue_;
}

bool mv::PassManager::validDescriptors() const
{

    if (!ready())
        return false;

    if (targetDescriptor_.getTarget() == Target::Unknown)
        return false;

    if (targetDescriptor_.getDType() == DType::Unknown)
        return false;
        
    if (targetDescriptor_.getOrder() == Order::Unknown)
        return false;

    auto checkStage = [this](const std::vector<std::string>& queue)
    {

        for (auto passIt = queue.begin(); passIt != queue.end(); ++passIt)
        {

            auto passPtr = pass::PassRegistry::instance().find(*passIt);
            if (passPtr->argsCount() > 0)
            {
                
                if (compDescriptor_.hasKey(passPtr->getName()))
                {
                    if (compDescriptor_[passPtr->getName()].valueType() != json::JSONType::Object)
                        return false;
                }
                else
                    return false;

                auto passArgs = passPtr->getArgs();
                for (auto argIt = passArgs.begin(); argIt != passArgs.end(); ++argIt)
                {
                    if (compDescriptor_[passPtr->getName()].hasKey(argIt->first))
                    {
                        if (compDescriptor_[passPtr->getName()][argIt->first].valueType() != argIt->second)
                            return false;
                    }
                    else
                        return false;
                }

            }

        }

        return true;

    };

    if (!checkStage(adaptPassQueue_))
        return false;

    if (!checkStage(optPassQueue_))
        return false;

    if (!checkStage(finalPassQueue_))
        return false;

    if (!checkStage(serialPassQueue_))
        return false;
    
    if (!checkStage(validPassQueue_))
        return false;

    return true;

}