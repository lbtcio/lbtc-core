
#ifndef _MY_SERIALIZE_H_
#define _MY_SERIALIZE_H_

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>


#include <fstream>
#include <map>
#include <vector>

template<typename A, typename B>
void SerializeData(A& a, B& b)
{
   a & BOOST_SERIALIZATION_NVP(b);
}

template<typename A, typename B, typename ... Args>
void SerializeData(A& a, B& b, Args& ... args)
{
   SerializeData(a, b);
   SerializeData(a, args...);
}

template<typename ... Args>
void MySerialize(const std::string& file, Args ... args)
{
   std::ofstream of(file);
   boost::archive::text_oarchive oa(of);
   SerializeData(oa, args...);
}

template<typename A, typename B>
void UnserializeData(A& a, B& b)
{
   a >> BOOST_SERIALIZATION_NVP(b);
}

template<typename A, typename B, typename ... Args>
void UnserializeData(A& a, B& b, Args& ... args)
{
   UnserializeData(a, b);
   UnserializeData(a, args...);
}

template<typename ... Args>
void MyUnserialize(const std::string& file, Args& ... arg)
{
   std::ifstream i(file);
   if(i.is_open()) {
      boost::archive::text_iarchive ia(i);

      UnserializeData(ia, arg...);
   }
}

#endif
