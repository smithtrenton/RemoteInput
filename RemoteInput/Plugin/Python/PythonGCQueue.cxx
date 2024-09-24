#include "PythonGCQueue.hxx"

PyGCQueue::PyGCQueue(EIOS* eios, size_t max_size) : eios(eios), max_size(max_size)
{
    std::cout << "new" << std::endl;
}

PyGCQueue::PyGCQueue(EIOS* eios) : eios(eios), max_size(PyGCQueue::DEFAULT_MAX_SIZE)
{
    std::cout << "new default" << std::endl;
}

PyGCQueue::~PyGCQueue()
{
    std::cout << "del" << std::endl;
    // eios = nullptr;
}

void PyGCQueue::add(jobject object)
{
    std::cout << "add" << std::endl;
    if (object != nullptr)
    {
        objects.insert(object);
        if (objects.size() >= max_size)
        {
            flush();
        }
    }
}

void PyGCQueue::flush()
{
    std::cout << "flush" << std::endl;
    if (objects.size() > 0) {
        std::vector<jobject> results;
        results.reserve(objects.size());
        for (auto it = objects.begin(); it != objects.end(); )
        {
            results.push_back(std::move(objects.extract(it++).value()));
        }
        Reflect_Release_Objects(eios.get(), &results[0], results.size());
        objects.clear();
    }
}
