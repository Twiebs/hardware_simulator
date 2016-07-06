
template<typename T>
struct DynamicArray {
  size_t capacity;
  size_t count;
  T *data;

  DynamicArray() {
    capacity = 0;
    count = 0;
    data = 0;
  }

  inline T& operator[](const size_t i){
    assert(i < count);
    return data[i];
  }
};

template<typename T>
void ArrayAdd(const T& t, DynamicArray<T>& array){
  if(array.count + 1 > array.capacity){
    array.capacity = array.capacity + 10;
    if(array.data != 0) array.data = (T*)realloc(array.data, sizeof(T) * array.capacity);
    else array.data = (T *)malloc(sizeof(T) * array.capacity);
  }

  array.data[array.count] = t;
  array.count += 1;
}

template<typename T>
void ArrayRemoveAtIndexUnordered(const size_t index, DynamicArray<T>& array){
  assert(index < array.count);
  array.data[index] = array.data[array.count-1];
  array.count -= 1;
}

template<typename T>
void ArrayRemoveValueUnordered(const T& t, DynamicArray<T>& array){
  for(size_t i = 0; i < array.count; i++){
    if(array.data[i] == t) {
      array.data[i] = array.data[array.count - 1];
      array.count -= 1;
      return;
    }
  }
  assert(false);
}

template <typename T>
int ArrayContains(const T& value, DynamicArray<T>& array){
  for(size_t i = 0; i < array.count; i++){
    if(array.data[i] == value){
      return 1;
    }
  }
  return 0;
}

template<typename T>
void ArrayDestroy(DynamicArray<T>& array){
  if(array.data != 0) free(array.data);
  array.count = 0;
  array.capacity = 0;
}


struct Rectangle {
  float minX, minY;
  float maxX, maxY;
};

int Intersects(const Rectangle& a, const Rectangle& b){
  if(a.maxX < b.minX || a.minX > b.maxX) return 0;
  if(a.maxY < b.minY || a.minY > b.maxY) return 0;
  return 1;
}

int Max(int a, int b){
  int result = a > b ? a : b;
  return result;
}

int Min(int a, int b){
  int result = a < b ? a : b;
  return result;
}