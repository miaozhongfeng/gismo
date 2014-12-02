#pragma once

#include <gsPde/gsPde.h>


namespace gismo
{

template <class T>
class gsFunction;

/** @brief
    A convection-diffusion-reaction PDE, including source term on the right-hand side.

    Any of the coefficients may be a NULL pointer, meaning that it is zero.
 */

template <class T>
class gsConvDiffRePde : public gsPde<T>
{
protected:
    using gsPde<T>::m_solution;
    using gsPde<T>::m_unknownDim;

public:
    gsConvDiffRePde(
        const gsMultiPatch<T>         &domain,
        const gsBoundaryConditions<T> &bc,
        const gsFunction<T> *diff, const gsFunction<T> *conv, const gsFunction<T> *reac, const gsFunction<T>  *rhs)
        : gsPde<T>(domain,bc),
            m_diff(diff), m_conv(conv), m_reac(reac), m_rhs(rhs)
    {
            m_unknownDim.push_back( rhs->targetDim() );
    }

    // COMPATIBILITY CONSTRUCTOR, DO NOT USE
    gsConvDiffRePde(
        const gsFunction<T> *diff, const gsFunction<T> *conv, const gsFunction<T> *reac, const gsFunction<T>  *rhs)
           : m_diff(diff), m_conv(conv), m_reac(reac), m_rhs(rhs)
    {
            m_unknownDim.push_back( rhs->targetDim() );
    }

    ~gsConvDiffRePde()
    {
    /*
        delete m_diff;
        delete m_conv;
        delete m_reac;
        delete m_rhs;
        */
    }

    virtual bool isSymmetric () const { gsWarn<<"Function is gsPde::isSymmetric should not be used!!"; return !(m_conv==NULL);}

    const gsFunction<T>* diffusion() const          { return m_diff; }
    const gsFunction<T>* convection() const         { return m_conv; }
    const gsFunction<T>* reaction() const           { return m_reac; }
    const gsFunction<T>* rhs() const                { return m_rhs; }

    std::ostream &print(std::ostream &os) const
    {
        os << "Convection-diffusion-reaction equation, with:\n";
        if (m_diff) os << "  Diffusion term: "  << *m_diff << "\n";
        if (m_conv) os << "  Convection term: " << *m_conv << "\n";
        if (m_reac) os << "  Reaction term: "   << *m_reac << "\n";
        if (m_rhs)  os << "  Source function: " << *m_rhs <<"\n";
        return os; 
    }
private:
    const gsFunction<T>* m_diff;
    const gsFunction<T>* m_conv;
    const gsFunction<T>* m_reac;
    const gsFunction<T>* m_rhs;
};

}
