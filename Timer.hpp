/*
Copyright 2013 Henrik Mühe

This file is part of Fekaton.

Fekaton is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fekaton is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with Fekaton.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <chrono>


class Timer
{
   std::chrono::high_resolution_clock::time_point start;
   
public:
   Timer() : start(std::chrono::high_resolution_clock::now()) {}
   
   void min() {
      start=std::chrono::high_resolution_clock::time_point::min();
   }
   
   void max() {
      start=std::chrono::high_resolution_clock::time_point::max();
   }
   
   void reset() {
      start=std::chrono::high_resolution_clock::now();
   }
   
   uint64_t getMicro() {
      return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()-start).count();
   }
   
   uint64_t getMicro(Timer& end) {
      return std::chrono::duration_cast<std::chrono::microseconds>(end.start-start).count();
   }
   
   uint64_t getMilli() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()-start).count();
   }
   
   uint64_t getMilli(Timer& end) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(end.start-start).count();
   }
   
   bool operator<(const Timer& other) const
   {
      return start<other.start;
   }
};

