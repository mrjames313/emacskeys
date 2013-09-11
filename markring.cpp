#include "markring.h"
#include "mark.h"

MarkRing::MarkRing()
  : iter(ring.begin())
{

}

void MarkRing::addMark(int position)
{
	Mark mark(position); // will be valid and active
	if (ring.isEmpty()) {
    ring.prepend(mark);
  }
	if(ring.first() != mark) {
		ring.first().active = false;
		ring.prepend(mark);
	}
  // shrink ring to default emacs max size
  while (ring.count() > 16) {
    ring.pop_back();
  }
  iter = ring.begin();
}

Mark MarkRing::getPreviousMark()
{
  if (ring.isEmpty()) {
    return Mark();
  }
	Mark retval = *iter;
	if (++iter == ring.end()) {
    iter = ring.begin();
  }
	return retval;
}

Mark MarkRing::getMostRecentMark()
{
  return ring.isEmpty() ? Mark() : ring.first();
}

void MarkRing::toggleActive()
{
	if(not ring.isEmpty()) {
		ring.first().active = not ring.first().active;
	}
}
