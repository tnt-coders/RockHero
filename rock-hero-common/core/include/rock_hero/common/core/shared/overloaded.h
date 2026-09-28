/*!
\file overloaded.h
\brief One callable per alternative of a variant, for exhaustive `std::visit`.
*/

#pragma once

namespace rock_hero::common::core
{

/*!
\brief Gathers one lambda per alternative into a single visitor, so a variant that gains an
alternative fails to compile at every visit that has not said what the new one means.

\tparam Handlers The lambdas, one per alternative.
*/
template <typename... Handlers> struct Overloaded : Handlers...
{
    using Handlers::operator()...;
};

} // namespace rock_hero::common::core
