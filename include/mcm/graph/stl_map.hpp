#ifndef STL_MAP_HPP_
#define STL_MAP_HPP_
    
#include <map>
#include "base_allocator.hpp"

namespace mv
{

    template<class T_key, class T_value, class T_allocator>
    class stl_map
    {

        std::map<T_key, T_value> stl_map_;
        const T_allocator allocator_;

        T_value dummy_value_;

    public:

        template <class T_iterator>
        class value_iterator : public T_iterator
        {

        public:

            value_iterator(T_iterator other) :
            T_iterator(other)
            {

            }

            value_iterator()
            {
                
            }

            const T_value& operator*() const
            {
                return T_iterator::operator*().second;
            }

        };

        /*using iterator = typename std::map<T_key, T_value>::iterator;
        using const_iterator = typename std::map<T_key, T_value>::const_iterator;
        using reverse_iterator = typename std::map<T_key, T_value>::reverse_iterator;*/

        using iterator = value_iterator<typename std::map<T_key, T_value>::iterator>;
        using const_iterator = value_iterator<typename std::map<T_key, T_value>::const_iterator>;
        using reverse_iterator = value_iterator<typename std::map<T_key, T_value>::reverse_iterator>;

        stl_map()
        {

        }

        /*stl_map(const stl_map &other):
        stl_map_(other.stl_map_)
        {

        }*/

        const_iterator cbegin() const noexcept
        {
            return stl_map_.cbegin();
        }

        const_iterator cend() const noexcept
        {
            return stl_map_.cend();
        }

        iterator begin() noexcept
        {
            return stl_map_.begin();
        }

        iterator end() noexcept
        {
            return stl_map_.end();
        }

        reverse_iterator rbegin() noexcept
        {
            return stl_map_.rbegin();
        }

        reverse_iterator rend() noexcept
        {
            return stl_map_.rend();
        }

        typename std::map<T_key, T_value>::size_type size() const noexcept
        {
            return stl_map_.size();
        }

        bool empty() const noexcept
        {
            return stl_map_.empty();
        }

        void clear() const noexcept
        {
            stl_map_.clear();
        }
        
        void erase(iterator &pos) noexcept
        {

            try
            {
                stl_map_.erase(pos);
            }
            catch (std::exception &e)
            {
                
            }
            
        }

        void erase(const T_key& key) noexcept
        {

            try
            {
                stl_map_.erase(key);
            }
            catch (std::exception &e)
            {
                
            }
            
        }
        
        iterator find(const T_key& key) noexcept
        {

            try
            {
                return stl_map_.find(key);
            }
            catch (std::exception &e)
            {
                return stl_map_.end();
            }

        }

        const_iterator find(const T_key& key) const noexcept
        {

            try
            {
                return stl_map_.find(key);
            }
            catch (std::exception &e)
            {
                return stl_map_.end();
            }

        }

        pair<iterator, bool> emplace(const T_key &key, const T_value &value)
        {
            auto result = stl_map_.emplace(key, value);
            return pair<iterator, bool>(result.first, result.second);
        }

        T_value& operator[](const T_key &key)
        {
            try
            {
                return stl_map_[key];
            }
            catch (std::exception &e)
            {
                return dummy_value_;
            }

        }

        const T_value& at(const T_key &key) const noexcept
        {
            try
            {
                return stl_map_.at(key);
            }
            catch (std::exception &e)
            {
                return dummy_value_;
            }

        }

        T_value& at(const T_key &key) noexcept
        {
            try
            {
                return stl_map_.at(key);
            }
            catch (std::exception &e)
            {
                return dummy_value_;
            }

        }

        stl_map& operator=(const stl_map &other) noexcept
        {
            stl_map_ = other.stl_map_;
            //allocator_ = other.allocator_;
            return *this;
        }

    };

}

#endif // STL_MAP_HPP_