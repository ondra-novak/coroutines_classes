#ifndef SRC_COCLASSES_PRIORITY_QUEUE
#define SRC_COCLASSES_PRIORITY_QUEUE

#include <queue>
#include "trailer.h"

namespace cocls {

///Extends standard priority queue with ability to pop and return item
template<typename _Tp, typename _Sequence = std::vector<_Tp>,
     typename _Compare  = std::less<typename _Sequence::value_type> >
class priority_queue: public std::priority_queue<_Tp, _Sequence, _Compare> {
public:

    using std::priority_queue<_Tp, _Sequence, _Compare>::priority_queue;

    using std::priority_queue<_Tp, _Sequence, _Compare>::pop;



    ///moves out top item and pops it;
    _Tp pop_item() {
        trailer x = [&]{this->pop();};
        return _Tp(std::move(this->c.front()));
    }



};


}



#endif
