#ifndef MV_WORKLOADS
#define MV_WORKLOADS

#include <string>
#include <array>
#include <functional>
#include "include/mcm/computation/model/model_element.hpp"
#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/base/exception/op_error.hpp"
#include "include/mcm/base/exception/index_error.hpp"
#include "include/mcm/tensor/shape.hpp"
#include "include/mcm/target/keembay/rectangle.hpp"
#include "include/mcm/target/keembay/workload_struct.hpp"
#include <metis.h>
#include <climits>
#include <math.h>
#include <cmath>
#include <vector>
#include <numeric>

namespace mv
{
    /**
    * @brief Cost Function types to be used when evaluating execution cycles of a workload
    */
    enum class CostFunctions
    {
        Balanced,
        CriticalPath,
        Greedy,
        MinMaxWorkloads
    };

    struct DPUMode { unsigned H, W; };
    using  DPUModeList = std::vector<mv::DPUMode>;
    using  DPUModeLists = std::vector<mv::DPUModeList>;

    /* The POC compiler generates a lattic structure of the tensor shape with the nodes numbered in this order
     * Example for tensor size 16x16
     * 
     *         axis numbering
     *     
     *         0    4     8    12     
     *        
     *    0    0----2-----4----6 //Even numbers
     *         |    |     |    | 
     *    4    1----3-----5----7 //Odd numbers
     *         |    |     |    | 
     *    8    10---11----12---13 // Incrementing numbers
     *         |    |     |    | 
     *    12   15---16----17---18
     */

    /* METIS parameters*/
    struct MetisGraphStructure
    {
        std::unique_ptr<idx_t[]>  xadj;   /*Indexes of starting points in adjacent array*/
        std::unique_ptr<idx_t[]>  adjncy; /*Adjacent vertices in consecutive index order*/
        std::unique_ptr<idx_t[]>  part;
        std::unique_ptr<idx_t[]>  vwgt;

        idx_t objval;
        idx_t nWeights  = 1;              /*Each vertex stores 1 weight*/
        idx_t options[METIS_NOPTIONS];

        idx_t m_numberTensorVertices;
        idx_t m_numberTensorEdges;
        int m_xDim;
        int m_yDim;
        int n_elem_y;
        int n_elem_x;
        double tensorXDim;
        double tensorYDim;

        std::unique_ptr<mv::Rectangle[]>  node_coords;

        MetisGraphStructure(mv::Shape outputTensor, mv::DPUMode MPEMode) {

            /*Shape of output tensor x-y*/
            tensorXDim = outputTensor[0]; 
            tensorYDim = outputTensor[1];

            /*Number of vertices and edges in METIS lattic graph of tensor*/
            m_numberTensorVertices = ceil(tensorXDim / MPEMode.H)  * ceil(tensorYDim / MPEMode.W);
            m_numberTensorEdges = (2 * ceil(tensorXDim / MPEMode.H) * ceil(tensorYDim / MPEMode.W)) - ceil(tensorXDim / MPEMode.H) - ceil(tensorYDim / MPEMode.W);
        
            /*X-Y dimension of METIS lattic graph*/
            m_xDim = ceil((tensorXDim / MPEMode.W));
            m_yDim = ceil((tensorYDim / MPEMode.H));

            /*METIS parameters - description page 23 Metis manual*/
            xadj.reset(new idx_t[m_numberTensorVertices + 1]);
            adjncy.reset(new idx_t[2*m_numberTensorEdges]);
            part.reset(new idx_t[m_numberTensorVertices]);
            vwgt.reset(new idx_t[m_numberTensorVertices* nWeights]);

            node_coords.reset(new mv::Rectangle [m_numberTensorVertices]);
        
            /* (1) This section gnerates weights for the METIS vertices
             * Description page 23 Metis manual
             * 
             * This is required when the tensor dimensions are not a multiple of 4 for MPE mode (4,4) or 16 for MPE mode (1,16)
             * When tensor size is not a multiple of the MPE dimensions a full DPUs will be fully utilised (i.e. < 256 multiplication operations)
             * Therefore we assign nodes different weights when partitioning
             * 
             * (2) We populate (x,y) coordinates for the individual lattic nodes here with the rectangle class. 
             * 
            */

            int nodeIndex = 0; /* This corresponds to the numbering format in the lattic structure*/

            /* We need to handle the first two rows of the lattic first, see node numbering in the lattic example above*/
            /* Here we populate the the coordiantes of the nodes in the lattic*/
            /* We need to handle the first two rows of the lattic first, see node numbering in the lattic example above*/
            for(int j=0; j < 1; j++) {
            
                if ((j+1 < m_yDim) || (!fmod(tensorYDim,MPEMode.H)))
                    n_elem_y = MPEMode.H;                 
                else 
                    n_elem_y = (int)tensorYDim%MPEMode.H; 
                
                /*This loops over the the first two rows 1,2,3,4 .... etc*/
                for(int k=0; k < (m_xDim*2); k++) {

                    int min_x;
                    int min_y;
                    
                    if((k%2 != 0) && (m_yDim <= 2)) 
                        n_elem_y = (int)tensorYDim%MPEMode.H;
                    else
                        n_elem_y = MPEMode.H; 
                    
                    if ((k < (m_xDim*2)-2) || (!fmod(tensorXDim,MPEMode.W)))
                        n_elem_x = MPEMode.W;
                    else 
                        n_elem_x = (int)tensorXDim%MPEMode.W;
                    
                    /*First row where node number is even i.e. 2,4,6... */
                    if ((nodeIndex%2 == 0) && (nodeIndex <= ((m_xDim*2)-2)))  { 

                        min_x = (k/2) * MPEMode.W;
                        min_y = j * MPEMode.H;

                        assert(nodeIndex < m_numberTensorVertices);
                        node_coords[nodeIndex] = mv::Rectangle(min_x, min_y, n_elem_x, n_elem_y);

                        assert(nodeIndex < m_numberTensorVertices * nWeights);
                        vwgt[nodeIndex] = n_elem_x * n_elem_y; /* Populate METIS weight*/
                    }
                    /*Second row where node number is odd i.e. 1,3,5... */
                    if ((nodeIndex%2 != 0) && (nodeIndex <= ((m_xDim*2)-1))) {
                        
                        /*For 7x7 tensor mode 4x4*/
                        if(m_yDim <= 2) {
                            min_x = min_x;
                            min_y = min_y + n_elem_y + 1;
                        }
                        else {
                            min_x = min_x;
                            min_y = min_y + n_elem_y;
                        }
                        
                        assert(nodeIndex < m_numberTensorVertices);
                        node_coords[nodeIndex] = mv::Rectangle(min_x, min_y, n_elem_x, n_elem_y);

                        assert(nodeIndex < m_numberTensorVertices * nWeights);
                        vwgt[nodeIndex] = n_elem_x * n_elem_y; /* Populate METIS weight*/
                    }        
                    nodeIndex++;
                }
}
            /* Now deal with the remaining rows after the first 2 rows*/
            for(int j=2; j < m_yDim; j++) { 
            
                if ((j+1 < m_yDim) || (!fmod(tensorYDim,MPEMode.H)))
                    n_elem_y = MPEMode.H;                 
                else 
                    n_elem_y = (int)tensorYDim%MPEMode.H; 
                            
                for(int k=0; k < m_xDim; k++) {

                    if ((k+1 < m_xDim) || (!fmod(tensorXDim,MPEMode.W)))
                        n_elem_x = MPEMode.W;
                    else 
                        n_elem_x = (int)tensorXDim%MPEMode.W;

                    assert(nodeIndex < m_numberTensorVertices * nWeights);
                    vwgt[nodeIndex] = n_elem_x * n_elem_y; /* Populate METIS weight*/

                    int min_x = k * MPEMode.W;
                    int min_y = j * MPEMode.H;

                    assert(nodeIndex < m_numberTensorVertices);
                    node_coords[nodeIndex] = mv::Rectangle(min_x, min_y, n_elem_x, n_elem_y);
                 
                    nodeIndex++;
                }
            }
        }
    };

    struct point
    {
        int16_t x = 0;
        int16_t y = 0;
    };

    enum class WorkloadSplitMode { HW=1, HC, WC };

    class Workloads : public LogSender
    {
        
        std::vector<Workload> workloads_;
        std::string layerName_;
        mv::Shape tensorShape_;
        std::vector<float> executionCycles_; //Min & Max execution cycles
        float meanExecutionCycles_ = 0;

        std::shared_ptr<MetisGraphStructure> metisGraph_;

        float critical_workload = 0;
        float workload_sum = 0;
        float min_range = 0;
        float max_range = 0;

        std::vector<int> generateMetisGraphNodeNumbers(void);

    public:
        Workloads(const std::string& name, const mv::Shape& tensorShape);
        Workloads(const std::string& name, const mv::Shape& tensorShape, mv::DPUMode& mpeMode);
        ~Workloads();
      
        void generateMetisGraph(void);
        std::shared_ptr<mv::MetisGraphStructure> getMetisGraph();
        int8_t clusterID = 0;

        /*returns: METIS_OK(=1), or METIS_ERROR*/
        int partitionTensorWithRectangleHeuristic(const mv::DPUModeList& modes,
                                                            idx_t        nWorkloads,
                                                            bool         split_over_h,
                                                            bool         split_over_w,
                                                            bool         split_symmetric,
                                                  const mv::WorkloadSplitMode& split_mode,
                                                  const mv::pass::PassEntry& pass);

        void populateWorkloadsFromPartitions(idx_t nWorkloads, 
                                            const mv::pass::PassEntry& pass, 
                                            mv::DPUMode& mpeMode);

        std::vector<mv::Workload> polygonWorkloadSplit(const mv::pass::PassEntry& pass, 
                                                        mv::Workload& workload, 
                                                        std::vector<mv::Workload>& workloads, 
                                                        mv::DPUMode& mpeMode);
        
        std::vector<mv::Workload> workloadSplitHelper(const mv::pass::PassEntry& pass, 
                                                        mv::Workload& workload, 
                                                        std::pair<std::pair<int16_t, int16_t>,bool>& interesting_point, 
                                                       mv::DPUMode& mpeMode);
        std::size_t nWorkloads() const;
        std::set<int> getNWorkloads(mv::Data::TensorIterator tensor);
        void addWorkload(mv::Workload workload);
        const std::vector<mv::Workload>& getWorkloads() const;
        static const std::vector<int> getWorkloadSplitPool(mv::Tensor& tensor, int nDPUxCluster, mv::DPUModeLists dpuModeLists, int maxSplits = 50);

        //void generateExecutionCycles(std::vector<mv::Data::TensorIterator>& outputTensor, int nDPUxCluster, CostFunctions costFunction);
        static void generateExecutionCycles(std::vector<mv::Workloads>& workloadsVector, int nDPUxCluster, CostFunctions costFunction);
        std::vector<float> getExecutionCycles() const;
        float getMeanExecutionCycles() const;
        void setExecutionCycles(std::vector<float> val);
        static float greedyTaskAssignment(int nProcessors, std::vector<float>& workloadCosts);

        bool validateWorkloads(std::vector<mv::Data::TensorIterator>& inputTensor);
        bool validateWorkloads(const mv::Shape& shape);

        static mv::CostFunctions getCostFunction(mv::Element& passDesc, const mv::pass::PassEntry& pass);

        double getAllWorkloadsVolume() const;
        bool noOverlap() const;
        mv::Shape getShapefromMinMax() const;

        Workload& operator[](int nworkload);
        bool operator < (const mv::Workloads& other) const;
        
        const Workload& operator[](int nworkload) const;
        std::string getLogID() const override;
        std::string toString() const;
        std::string toLongString() const;
    };
}

#endif 
