#include <string.h>
#include <maya/MIOStream.h>
#include <math.h>

#include <maya/MPxDeformerNode.h>
#include <maya/MItGeometry.h>

#include <maya/MTypeId.h>
#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MTypes.h>

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnPlugin.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMesh.h>

#include <maya/MPoint.h>
#include <maya/MMatrix.h>
#include <maya/MVector.h>
#include <maya/MBoundingBox.h>
#include <maya/MPointArray.h>
#include <maya/MTransformationMatrix.h>

#define MCheckErr( stat, msg )      \
    if ( MS::kSuccess != stat ) {   \
        cerr << msg;                \
        return MS::kFailure;        \
    }

// Dimensions (subdivisions) along the S-axis of the FFD lattice.
#define FFD_DIMENSIONS_S 2
// Number of vertices along the S-axis of the FFD lattice.
#define FFD_LATTICE_POINTS_S FFD_DIMENSIONS_S + 1

#define FFD_DIMENSIONS_T 0
#define FFD_LATTICE_POINTS_T FFD_DIMENSIONS_T + 1

#define FFD_DIMENSIONS_U 2
#define FFD_LATTICE_POINTS_U FFD_DIMENSIONS_U + 1

/*!
 * A deformer that implements a 2x0x2 FFD with nine control points on the lattice.
 * The current implementation only supports displacement of the control points along the y-axis (height).
 */
class ffdPlanar : public MPxDeformerNode
{
public:
                            ffdPlanar();
    virtual                 ~ffdPlanar();

    static  void*           creator();
    static  MStatus         initialize();

    // Deformation function.
    virtual MStatus         deform( MDataBlock& 	block,
                                    MItGeometry& 	iter,
                                    const MMatrix& 	mat,
                                    unsigned int 	multiIndex );

private:
    MStatus                 getBoundingBox( MDataBlock& block,
                                            unsigned int multiIndex,
                                            MBoundingBox& boundingBoxOut );
    MTransformationMatrix   getXyzToStuTransformation( MBoundingBox& boundingBox );
    MPoint                  getDeformedPoint( MPoint& point,
                                              MVector (& lattice)[FFD_LATTICE_POINTS_S][FFD_LATTICE_POINTS_T][FFD_LATTICE_POINTS_U] );
    static double           bernsteinPoly( int i, int n, double t);
    static int              nChooseK( int n, int k);

public:
    // ffdPlanar attributes to expose to Maya UI.
    static  MTypeId         id;
    
    static  MObject         latticeRow1;
    static  MObject         latticeRow2;
    static  MObject         latticeRow3;

private:

};

/**************
 * ATTRIBUTES *
 **************/

MTypeId     ffdPlanar::id( 0x8000a );

MObject     ffdPlanar::latticeRow1;
MObject     ffdPlanar::latticeRow2;
MObject     ffdPlanar::latticeRow3;

/*!
 * Constructor.
 */
ffdPlanar::ffdPlanar() {}

/*!
 * Destructor.
 */
ffdPlanar::~ffdPlanar() {}

/*!
 * Node creator for Maya.
 */
void* ffdPlanar::creator()
{
    return new ffdPlanar();
}

/*!
 * Initialize attributes in Maya.
 */
MStatus ffdPlanar::initialize()
{
    // Local attributes.
    MFnNumericAttribute nAttr;
    
    latticeRow1 = nAttr.create( "row1", "r1", MFnNumericData::k3Double );
    nAttr.setDefault( 0.0, 0.0, 0.0 );
    nAttr.setMin( -100.0, -100.0, -100.0 );
    nAttr.setMax( 100.0, 100.0, 100.0 );
    nAttr.setKeyable( true );
    addAttribute( latticeRow1 );
    
    latticeRow2 = nAttr.create( "row2", "r2", MFnNumericData::k3Double );
    nAttr.setDefault( 0.0, 0.0, 0.0 );
    nAttr.setMin( -100.0, -100.0, -100.0 );
    nAttr.setMax( 100.0, 100.0, 100.0 );
    nAttr.setKeyable( true );
    addAttribute( latticeRow2 );
    
    latticeRow3 = nAttr.create( "row3", "r3", MFnNumericData::k3Double );
    nAttr.setDefault( 0.0, 0.0, 0.0 );
    nAttr.setMin( -100.0, -100.0, -100.0 );
    nAttr.setMax( 100.0, 100.0, 100.0 );
    nAttr.setKeyable( true );
    addAttribute( latticeRow3 );
    
    // Attributes affect geometry.
    attributeAffects( ffdPlanar::latticeRow1, ffdPlanar::outputGeom );
    attributeAffects( ffdPlanar::latticeRow2, ffdPlanar::outputGeom );
    attributeAffects( ffdPlanar::latticeRow3, ffdPlanar::outputGeom );

    return MS::kSuccess;
}

/*!
 * Description:   Deform the point using the Sederberg-Parry FFD algorithm.
 *
 * Arguments:
 *  block       : the datablock of the node
 *  iter        : an iterator for the geometry to be deformed
 *  m           : matrix to transform the point into world space
 *  multiIndex  : the index of the geometry that we are deforming
 */
MStatus ffdPlanar::deform( MDataBlock& block,
                          MItGeometry& iter,
                          const MMatrix& /*m*/,
                          unsigned int multiIndex )
{
    MStatus status = MS::kSuccess;
    
    // Determine the displacement lattice points.
    MDataHandle row1Data = block.inputValue( latticeRow1, &status );
    MCheckErr( status, "Error getting r1 data handle\n" );
    MVector row1Vector = row1Data.asVector();
    
    MDataHandle row2Data = block.inputValue( latticeRow2, &status );
    MCheckErr( status, "Error getting r2 data handle\n" );
    MVector row2Vector = row2Data.asVector();
    
    MDataHandle row3Data = block.inputValue( latticeRow3, &status );
    MCheckErr( status, "Error getting r3 data\n" );
    MVector row3Vector = row3Data.asVector();

    // Determine the envelope (this is a global scale factor for the deformer).
    MDataHandle envData = block.inputValue(envelope,&status);
    MCheckErr(status, "Error getting envelope data handle\n");
    float env = envData.asFloat();
    
    // Generate the FFD lattice.
    MVector lattice[FFD_LATTICE_POINTS_S][FFD_LATTICE_POINTS_T][FFD_LATTICE_POINTS_U] = { // Since dimensions known ahead of time, generate array now.
        { // x = 0
            { MVector(0.f, row1Vector.x, 0.f), MVector(0.f, row1Vector.y, .5f), MVector(0.f, row1Vector.z, 1.f) }, // y = 0
        },
        { // x = 1
            { MVector(.5f, row2Vector.x, 0.f), MVector(.5f, row2Vector.y, .5f), MVector(.5f, row2Vector.z, 1.f) }, // y = 0
        },
        { // x = 2
            { MVector(1.f, row3Vector.x, 0.f), MVector(1.f, row3Vector.y, .5f), MVector(1.f, row3Vector.z, 1.f) }, // y = 0
        }
    };
    
    MBoundingBox boundingBox;
    status = getBoundingBox( block, multiIndex, boundingBox );
    MCheckErr( status, "Error getting bounding box\n" );
    
    MTransformationMatrix transform = getXyzToStuTransformation( boundingBox );
    MMatrix transformMatrix = transform.asMatrix();
    MMatrix inverseMatrix = transform.asMatrixInverse();
    
    // Iterate through each point in the geometry.
    for ( ; !iter.isDone(); iter.next() )
    {
        MPoint pt = iter.position();
        MPoint ptStu = pt * transformMatrix;
        MPoint deformed = getDeformedPoint( ptStu, lattice ) * inverseMatrix;

        if ( env != 1.f )
        {
            MVector diff = deformed - pt;
            deformed = pt + env * diff;
        }
        
        iter.setPosition( deformed );
    }
    return status;
}

MStatus ffdPlanar::getBoundingBox( MDataBlock& block,
                                   unsigned int multiIndex,
                                   MBoundingBox &boundingBoxOut )
{
    MStatus status = MS::kSuccess;
    
    MArrayDataHandle inputHandle = block.outputArrayValue( input );
    inputHandle.jumpToElement( multiIndex );
    MObject mesh = inputHandle.outputValue().child( inputGeom ).asMesh();
    
    MBoundingBox boundingBox = MBoundingBox();
    MFnMesh meshFn( mesh, &status );
    MCheckErr( status, "Error getting mesh from mesh object\n" );
    
    MPointArray pointArray = MPointArray();
    meshFn.getPoints( pointArray, MSpace::kTransform );
    
    for ( int i = 0; i < pointArray.length(); i++ )
    {
        boundingBox.expand( pointArray[i] );
    }
    
    boundingBoxOut = boundingBox;
    return status;
}

MTransformationMatrix ffdPlanar::getXyzToStuTransformation( MBoundingBox& boundingBox )
{
    MTransformationMatrix transform = MTransformationMatrix();
    
    double scale[3] = { FFD_DIMENSIONS_S > 0 ? 1.f / boundingBox.width() : 1.f,
                        FFD_DIMENSIONS_T > 0 ? 1.f / boundingBox.height() : 1.f,
                        FFD_DIMENSIONS_U > 0 ? 1.f / boundingBox.depth() : 1.f };
    
    transform.addScale( scale, MSpace::kObject );
    
    MVector boundsMinOffset = MPoint::origin - boundingBox.min();
    transform.addTranslation( boundsMinOffset, MSpace::kObject );
    
    return transform;
}

/*!
 * Deforms a point in the STU space into another point in the STU space using a lattice.
 */
MPoint ffdPlanar::getDeformedPoint( MPoint& point,
                                    MVector (& lattice)[FFD_LATTICE_POINTS_S][FFD_LATTICE_POINTS_T][FFD_LATTICE_POINTS_U] )
{
    MPoint ffd1 = MPoint();
    MVector ffd2 = MVector();
    MVector ffd3 = MVector();
    
    for ( int i = 0; i < FFD_LATTICE_POINTS_S; i++ )
    {
        for ( int j = 0; j < FFD_LATTICE_POINTS_T; j++ )
        {
            for ( int k = 0; k < FFD_LATTICE_POINTS_U; k++ )
            {
                double bernstein_u = bernsteinPoly( k, FFD_DIMENSIONS_U, point.z);
                ffd3 += lattice[i][j][k] * bernstein_u;
            }
            
            double bernstein_t = bernsteinPoly( j, FFD_DIMENSIONS_T, point.y );
            ffd2 += ffd3 * bernstein_t;
                        
            ffd3.x = 0;
            ffd3.y = 0;
            ffd3.z = 0;
        }
        
        double bernstein_s = bernsteinPoly( i, FFD_DIMENSIONS_S, point.x );
        ffd1 += ffd2 * bernstein_s;
        
        ffd2.x = 0;
        ffd2.y = 0;
        ffd2.z = 0;
    }
    
    return ffd1;
}

/*!
 * Formula for Bernstein polynomials taken from the Sederberg & Parry paper.
 */
double ffdPlanar::bernsteinPoly( int i, int n, double s )
{
    return nChooseK(n, i) * pow(1 - s, (n - i)) * pow(s, i);
}

/*!
 * Multiplicative formula for calculatin the binomial coefficient for nCk.
 */
int ffdPlanar::nChooseK( int n, int k )
{
    if ( k < 0 || k > n )
    {
        return 0;
    }
    
    if ( k > n - k )
    {
        k = n - k;
    }
    
    int c = 1;
    for (int i = 1; i <= k; i++) {
        c = c * (n - (k - i));
        c = c / i;
    }
    
    return c;
}


/**********************************
 * STANDARD PLUGIN INITIALIZATION *
 **********************************/

/*!
 * Registers the plugin in Maya.
 */
MStatus initializePlugin( MObject obj )
{
    MStatus result;
    MFnPlugin plugin( obj, PLUGIN_COMPANY, "3.0", "Any" );
    result = plugin.registerNode( "ffdPlanar", ffdPlanar::id, ffdPlanar::creator, 
                                  ffdPlanar::initialize, MPxNode::kDeformerNode );
    return result;
}

/*!
 * Deregisters the plugin from Maya.
 */
MStatus uninitializePlugin( MObject obj )
{
    MStatus result;
    MFnPlugin plugin( obj );
    result = plugin.deregisterNode( ffdPlanar::id );
    return result;
}
