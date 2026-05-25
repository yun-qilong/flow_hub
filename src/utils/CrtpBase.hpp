// CRTP utility — provides getImplementation() for compile-time polymorphism.
//
// Intermediate base classes inherit CrtpBase to call derived-class
// specializations via this->getImplementation().method().
//
// template <typename Derived> class MyBase : public utils::CrtpBase<Derived>
// {
//   protected:
//     void doWork()
//     {
//         this->getImplementation().actualWork(); // calls Derived::actualWork()
//     }
// };
//
// class MyClass : public MyBase<MyClass>
// {
//   public:
//     void actualWork()
//     {
//       /* derived specialization */
//     }
// };

#pragma once

namespace utils
{

template <typename Derived>
class CrtpBase
{
  protected:
    [[nodiscard]] Derived &getImplementation() noexcept
    {
        return static_cast<Derived &>(*this);
    }

    [[nodiscard]] const Derived &getImplementation() const noexcept
    {
        return static_cast<const Derived &>(*this);
    }
};

} // namespace utils
