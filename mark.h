#ifndef MARK_H
#define MARK_H

struct Mark 
{
  Mark(int position)
		: valid(true), active(true), position(position)
  {
  }
  Mark()
		: valid(false), active(false)
  {
  }
  bool operator ==(const Mark& mark)
  {
    return valid == mark.valid && position == mark.position;
  }
  bool operator !=(const Mark& mark)
  {
    return !(*this == mark);
  }
  bool valid;
	bool active;
  int position;
};

#endif
