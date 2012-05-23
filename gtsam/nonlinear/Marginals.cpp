/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation, 
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file Marginals.cpp
 * @brief 
 * @author Richard Roberts
 * @date May 14, 2012
 */

#include <gtsam/3rdparty/Eigen/Eigen/Dense>
#include <gtsam/linear/GaussianSequentialSolver.h>
#include <gtsam/linear/GaussianMultifrontalSolver.h>
#include <gtsam/nonlinear/Marginals.h>

namespace gtsam {

/* ************************************************************************* */
Marginals::Marginals(const NonlinearFactorGraph& graph, const Values& solution, Factorization factorization) {

  // Compute COLAMD ordering
  ordering_ = *graph.orderingCOLAMD(solution);

  // Linearize graph
  graph_ = *graph.linearize(solution, ordering_);

  // Store values
  values_ = solution;

  // Compute BayesTree
  factorization_ = factorization;
  if(factorization_ == CHOLESKY)
    bayesTree_ = *GaussianMultifrontalSolver(graph_, false).eliminate();
  else if(factorization_ == QR)
    bayesTree_ = *GaussianMultifrontalSolver(graph_, true).eliminate();
}

/* ************************************************************************* */
void Marginals::print(const std::string& str, const KeyFormatter& keyFormatter) const {
	ordering_.print(str+"Ordering: ", keyFormatter);
	graph_.print(str+"Graph: ");
	values_.print(str+"Solution: ", keyFormatter);
	bayesTree_.print(str+"Bayes Tree: ");
}

/* ************************************************************************* */
Matrix Marginals::marginalCovariance(Key variable) const {
  return marginalInformation(variable).inverse();
}

/* ************************************************************************* */
Matrix Marginals::marginalInformation(Key variable) const {
  // Get linear key
  Index index = ordering_[variable];

  // Compute marginal
  GaussianFactor::shared_ptr marginalFactor;
  if(factorization_ == CHOLESKY)
    marginalFactor = bayesTree_.marginalFactor(index, EliminatePreferCholesky);
  else if(factorization_ == QR)
    marginalFactor = bayesTree_.marginalFactor(index, EliminateQR);

  // Get information matrix (only store upper-right triangle)
  if(typeid(*marginalFactor) == typeid(JacobianFactor)) {
    JacobianFactor::constABlock A = static_cast<const JacobianFactor&>(*marginalFactor).getA();
    return A.transpose() * A; // Compute A'A
  } else if(typeid(*marginalFactor) == typeid(HessianFactor)) {
    const HessianFactor& hessian = static_cast<const HessianFactor&>(*marginalFactor);
    const size_t dim = hessian.getDim(hessian.begin());
    return hessian.info().topLeftCorner(dim,dim).selfadjointView<Eigen::Upper>(); // Take the non-augmented part of the information matrix
  } else {
    throw runtime_error("Internal error: Marginals::marginalInformation expected either a JacobianFactor or HessianFactor");
  }
}

/* ************************************************************************* */
JointMarginal Marginals::jointMarginalCovariance(const std::vector<Key>& variables) const {
  JointMarginal info = jointMarginalInformation(variables);
  info.fullMatrix_ = info.fullMatrix_.inverse();
  return info;
}

/* ************************************************************************* */
JointMarginal Marginals::jointMarginalInformation(const std::vector<Key>& variables) const {

  // If 2 variables, we can use the BayesTree::joint function, otherwise we
  // have to use sequential elimination.
  if(variables.size() == 1) {
    Matrix info = marginalInformation(variables.front());
    std::vector<size_t> dims;
    dims.push_back(info.rows());
    Ordering indices;
    indices.insert(variables.front(), 0);
    return JointMarginal(info, dims, indices);

  } else {
    // Convert keys to linear indices
    vector<Index> indices(variables.size());
    for(size_t i=0; i<variables.size(); ++i) { indices[i] = ordering_[variables[i]]; }

    // Compute joint factor graph
    GaussianFactorGraph jointFG;
    if(variables.size() == 2) {
      if(factorization_ == CHOLESKY)
        jointFG = *bayesTree_.joint(indices[0], indices[1], EliminatePreferCholesky);
      else if(factorization_ == QR)
        jointFG = *bayesTree_.joint(indices[0], indices[1], EliminateQR);
    } else {
      if(factorization_ == CHOLESKY)
        jointFG = *GaussianSequentialSolver(graph_, false).jointFactorGraph(indices);
      else if(factorization_ == QR)
        jointFG = *GaussianSequentialSolver(graph_, true).jointFactorGraph(indices);
    }

    // Conversion from variable keys to position in factor graph variables,
    // which are sorted in index order.
    Ordering variableConversion;
    {
      FastMap<Index,Key> usedIndices;
      for(size_t i=0; i<variables.size(); ++i)
        usedIndices.insert(make_pair(indices[i], variables[i]));
      size_t slot = 0;
      typedef pair<Index,Key> Index_Key;
      BOOST_FOREACH(const Index_Key& index_key, usedIndices) {
        variableConversion.insert(index_key.second, slot);
        ++ slot;
      }
    }

    // Get dimensions from factor graph
    std::vector<size_t> dims(indices.size(), 0);
    for(size_t i = 0; i < variables.size(); ++i)
      dims[i] = values_.at(variables[i]).dim();

    // Get information matrix
    Matrix augmentedInfo = jointFG.denseHessian();
    Matrix info = augmentedInfo.topLeftCorner(augmentedInfo.rows()-1, augmentedInfo.cols()-1);

    return JointMarginal(info, dims, variableConversion);
  }
}

} /* namespace gtsam */