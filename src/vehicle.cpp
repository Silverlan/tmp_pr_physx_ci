#include "pr_physx/vehicle.hpp"
#include "pr_physx/environment.hpp"
#include "pr_physx/collision_object.hpp"
#include "pr_physx/query_filter_callback.hpp"
#include <vehicle/PxVehicleUtil.h>

enum
{
	DRIVABLE_SURFACE = 64,
	UNDRIVABLE_SURFACE = 32
};
pragma::physics::PhysXVehicle &pragma::physics::PhysXVehicle::GetVehicle(IVehicle &c)
{
	return *static_cast<PhysXVehicle*>(c.GetUserData());
}
const pragma::physics::PhysXVehicle &GetVehicle(const pragma::physics::IVehicle &v) {return GetVehicle(const_cast<pragma::physics::IVehicle&>(v));}
pragma::physics::PhysXVehicle::PhysXVehicle(IEnvironment &env,PhysXUniquePtr<physx::PxVehicleDrive> vhc,const util::TSharedHandle<ICollisionObject> &collisionObject)
	: IVehicle{env,collisionObject},m_vehicle{std::move(vhc)}
{
	SetUserData(this);
}
physx::PxVehicleDrive &pragma::physics::PhysXVehicle::GetInternalObject() const {return *m_vehicle;}

namespace snippetvehicle
{

	using namespace physx;

	enum
	{
		DRIVABLE_SURFACE = 0xffff0000,
		UNDRIVABLE_SURFACE = 0x0000ffff
	};

	void setupDrivableSurface(PxFilterData& filterData);

	void setupNonDrivableSurface(PxFilterData& filterData);

	//Data structure for quick setup of scene queries for suspension queries.
	class VehicleSceneQueryData
	{
	public:
		VehicleSceneQueryData();
		~VehicleSceneQueryData();

		//Allocate scene query data for up to maxNumVehicles and up to maxNumWheelsPerVehicle with numVehiclesInBatch per batch query.
		static VehicleSceneQueryData* allocate
		(const PxU32 maxNumVehicles, const PxU32 maxNumWheelsPerVehicle, const PxU32 maxNumHitPointsPerWheel, const PxU32 numVehiclesInBatch,
			PxBatchQueryPreFilterShader preFilterShader, PxBatchQueryPostFilterShader postFilterShader);

		//Free allocated buffers.
		void free(PxAllocatorCallback& allocator);

		//Create a PxBatchQuery instance that will be used for a single specified batch.
		static PxBatchQuery* setUpBatchedSceneQuery(const PxU32 batchId, const VehicleSceneQueryData& vehicleSceneQueryData, PxScene* scene);

		//Return an array of scene query results for a single specified batch.
		PxRaycastQueryResult* getRaycastQueryResultBuffer(const PxU32 batchId); 

		//Return an array of scene query results for a single specified batch.
		PxSweepQueryResult* getSweepQueryResultBuffer(const PxU32 batchId); 

		//Get the number of scene query results that have been allocated for a single batch.
		PxU32 getQueryResultBufferSize() const; 

	private:

		//Number of queries per batch
		PxU32 mNumQueriesPerBatch;

		//Number of hit results per query
		PxU32 mNumHitResultsPerQuery;

		//One result for each wheel.
		PxRaycastQueryResult* mRaycastResults;
		PxSweepQueryResult* mSweepResults;

		//One hit for each wheel.
		PxRaycastHit* mRaycastHitBuffer;
		PxSweepHit* mSweepHitBuffer;

		//Filter shader used to filter drivable and non-drivable surfaces
		PxBatchQueryPreFilterShader mPreFilterShader;

		//Filter shader used to reject hit shapes that initially overlap sweeps.
		PxBatchQueryPostFilterShader mPostFilterShader;

	};

} // namespace snippetvehicle

void incrementDrivingMode(const physx::PxF32 timestep);
extern physx::PxVehicleDrive4WRawInputData gVehicleInputData;
extern physx::PxF32					gVehicleModeLifetime;
extern physx::PxF32					gVehicleModeTimer;
extern physx::PxU32					gVehicleOrderProgress;
extern bool					gVehicleOrderComplete;
extern bool					gMimicKeyInputs;
extern physx::PxF32 gLengthScale;
extern snippetvehicle::VehicleSceneQueryData*	gVehicleSceneQueryData;
extern physx::PxBatchQuery*			gBatchQuery;

extern physx::PxVehicleDrivableSurfaceToTireFrictionPairs* gFrictionPairs;

extern physx::PxRigidStatic*			gGroundPlane;
extern physx::PxVehicleDrive4W*		gVehicle4W;

extern bool					gIsVehicleInAir;
physx::PxVehicleKeySmoothingData gKeySmoothingData=
{
	{
		6.0f,	//rise rate eANALOG_INPUT_ACCEL
		6.0f,	//rise rate eANALOG_INPUT_BRAKE		
		6.0f,	//rise rate eANALOG_INPUT_HANDBRAKE	
		2.5f,	//rise rate eANALOG_INPUT_STEER_LEFT
		2.5f,	//rise rate eANALOG_INPUT_STEER_RIGHT
	},
	{
		10.0f,	//fall rate eANALOG_INPUT_ACCEL
		10.0f,	//fall rate eANALOG_INPUT_BRAKE		
		10.0f,	//fall rate eANALOG_INPUT_HANDBRAKE	
		5.0f,	//fall rate eANALOG_INPUT_STEER_LEFT
		5.0f	//fall rate eANALOG_INPUT_STEER_RIGHT
	}
};

physx::PxVehiclePadSmoothingData gPadSmoothingData=
{
	{
		6.0f,		//rise rate eANALOG_INPUT_ACCEL
		6.0f,		//rise rate eANALOG_INPUT_BRAKE		
		6.0f,		//rise rate eANALOG_INPUT_HANDBRAKE	
		2.5f,		//rise rate eANALOG_INPUT_STEER_LEFT
		2.5f,		//rise rate eANALOG_INPUT_STEER_RIGHT
	},
	{
		10.0f,		//fall rate eANALOG_INPUT_ACCEL
		10.0f,		//fall rate eANALOG_INPUT_BRAKE		
		10.0f,		//fall rate eANALOG_INPUT_HANDBRAKE	
		5.0f,		//fall rate eANALOG_INPUT_STEER_LEFT
		5.0f		//fall rate eANALOG_INPUT_STEER_RIGHT
	}
};
extern physx::PxF32 gSteerVsForwardSpeedData[2*8];
extern physx::PxFixedSizeLookupTable<8> gSteerVsForwardSpeedTable;
void pragma::physics::PhysXVehicle::Simulate(float dt)
{
	const physx::PxF32 timestep = dt;//1.0f/60.0f;

	//Cycle through the driving modes to demonstrate how to accelerate/reverse/brake/turn etc.
	incrementDrivingMode(timestep);

	//Update the control inputs for the vehicle.
	if(gMimicKeyInputs)
	{
		physx::PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(gKeySmoothingData, gSteerVsForwardSpeedTable, gVehicleInputData, timestep, gIsVehicleInAir, *gVehicle4W);
	}
	else
	{
		physx::PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(gPadSmoothingData, gSteerVsForwardSpeedTable, gVehicleInputData, timestep, gIsVehicleInAir, *gVehicle4W);
	}

	//Raycasts.
	physx::PxVehicleWheels* vehicles[1] = {gVehicle4W};
	physx::PxRaycastQueryResult* raycastResults = gVehicleSceneQueryData->getRaycastQueryResultBuffer(0);
	const physx::PxU32 raycastResultsSize = gVehicleSceneQueryData->getQueryResultBufferSize();
	physx::PxVehicleSuspensionRaycasts(gBatchQuery, 1, vehicles, raycastResultsSize, raycastResults);

	//Vehicle update.
	const physx::PxVec3 grav = GetPxEnv().GetScene().getGravity();//ToPhysXVector(Vector3{0.f,-9.81f *40.f,0.f});//gScene->getGravity();
	physx::PxWheelQueryResult wheelQueryResults[PX_MAX_NB_WHEELS];
	physx::PxVehicleWheelQueryResult vehicleQueryResults[1] = {{wheelQueryResults, gVehicle4W->mWheelsSimData.getNbWheels()}};
	PxVehicleUpdates(timestep, grav, *gFrictionPairs, 1, vehicles, vehicleQueryResults);

	//Work out if the vehicle is in the air.
	gIsVehicleInAir = gVehicle4W->getRigidDynamicActor()->isSleeping() ? false : PxVehicleIsInAir(vehicleQueryResults[0]);

	if(gVehicle4W->getRigidDynamicActor()->isSleeping())
		Con::cerr<<"ASLEEP!"<<Con::endl;
	if(gIsVehicleInAir)
		std::cout<<"Vehicle in air!"<<std::endl;
	std::cout<<"Tire friction: "<<vehicleQueryResults[0].wheelQueryResults->tireFriction<<std::endl;
	auto *surfMat = vehicleQueryResults[0].wheelQueryResults->tireSurfaceMaterial;
	if(surfMat)
		std::cout<<"SurfMat: "<<surfMat->getDynamicFriction()<<","<<surfMat->getStaticFriction()<<","<<surfMat->getRestitution()<<std::endl;
	/*std::array<physx::PxVehicleWheels*,1> vehicles = {m_vehicle.get()};
	PxVehicleSuspensionRaycasts(m_raycastBatchQuery.get(),vehicles.size(),vehicles.data(),GetWheelCount(),m_raycastQueryResultPerWheel.data());

	Vector3 gravity {};
	auto *pColObj = GetCollisionObject();
	if(pColObj != nullptr)
		gravity = pColObj->GetGravity();

	gravity = {0.f,-6'000.f,0.f}; // TODO

	std::array<physx::PxVehicleWheelQueryResult,1> vehicleWheelQueryResults = {{m_wheelQueryResults.data(),m_wheelQueryResults.size()}};
	physx::PxVehicleUpdates(dt,GetPxEnv().ToPhysXVector(gravity),GetPxEnv().GetVehicleSurfaceTireFrictionPairs(),vehicles.size(),vehicles.data(),vehicleWheelQueryResults.data());

	
	// Check
	auto numInAir = 0;
	auto avgTireFriction = 0.f;
	auto &queryResult = vehicleWheelQueryResults.front();
	for(auto i=decltype(queryResult.nbWheelQueryResults){0u};i<queryResult.nbWheelQueryResults;++i)
	{
		auto &result = queryResult.wheelQueryResults[i];
		if(result.isInAir)
			++numInAir;
		avgTireFriction += result.tireFriction;
	}
	if(physx::PxVehicleIsInAir(vehicleWheelQueryResults.front()))
		Con::cout<<"Vehicle is in the air!"<<Con::endl;
	Con::cout<<"Tire Friction: "<<(avgTireFriction /static_cast<float>(m_wheelQueryResults.size()))<<Con::endl;
	if(vehicleWheelQueryResults.front().wheelQueryResults->tireContactActor && vehicleWheelQueryResults.front().wheelQueryResults->tireContactActor->is<physx::PxRigidActor>())
	{
		auto *pCollisionObject = PhysXEnvironment::GetCollisionObject(*vehicleWheelQueryResults.front().wheelQueryResults->tireContactActor->is<physx::PxRigidActor>());
		if(pCollisionObject)
			Con::cout<<"HIT COLLISION OBJECT!"<<Con::endl;
	}*/
}
void pragma::physics::PhysXVehicle::Initialize()
{
	IVehicle::Initialize();
	// TODO
	// GetInternalObject().setUserData(this);

	auto numWheels = GetWheelCount();
	m_raycastQueryResultPerWheel.resize(numWheels);
	m_wheelQueryResults.resize(numWheels);
	m_raycastHitPerWheel.resize(numWheels);

	physx::PxBatchQueryDesc sqDesc {numWheels,0,0};
	sqDesc.queryMemory.userRaycastResultBuffer = m_raycastQueryResultPerWheel.data();
	sqDesc.queryMemory.userRaycastTouchBuffer = m_raycastHitPerWheel.data();
	sqDesc.queryMemory.raycastTouchBufferSize = numWheels;
	sqDesc.preFilterShader = [](
		physx::PxFilterData queryFilterData, physx::PxFilterData objectFilterData,
		const void* constantBlock, physx::PxU32 constantBlockSize,
		physx::PxHitFlags& hitFlags
	) -> physx::PxQueryHitType::Enum {
			//return (((objectFilterData.word3 & DRIVABLE_SURFACE) == 0) ?
			//	physx::PxQueryHitType::eNONE : physx::PxQueryHitType::eBLOCK);
			return (((objectFilterData.word3 & UNDRIVABLE_SURFACE) != 0) ?
				physx::PxQueryHitType::eNONE : physx::PxQueryHitType::eBLOCK);
	};
	m_raycastBatchQuery = px_create_unique_ptr(GetPxEnv().GetScene().createBatchQuery(sqDesc));


	//m_vehicle->mDriveDynData.setUseAutoGears(true);
	//m_vehicle->mDriveDynData.setAnalogInput(physx::PxVehicleDrive4WControl::eANALOG_INPUT_ACCEL,1.0);
//m_vehicle->mDriveDynData.forceGearChange(1);
	//m_vehicle->mDriveDynData.setAutoBoxSwitchTime(0.f);

	//m_vehicle->mDriveDynData.setEngineRotationSpeed(100.f);
	//m_vehicle->mWheelsDynData.setWheelRotationSpeed(0,100.f);
	//vehDrive4W->mDriveDynData.
}
uint32_t pragma::physics::PhysXVehicle::GetWheelCount() const {return 4;}
float pragma::physics::PhysXVehicle::GetForwardSpeed() const
{
	return GetPxEnv().FromPhysXLength(m_vehicle->computeForwardSpeed());
}
float pragma::physics::PhysXVehicle::GetSidewaysSpeed() const
{
	return GetPxEnv().FromPhysXLength(m_vehicle->computeSidewaysSpeed());
}
void pragma::physics::PhysXVehicle::RemoveWorldObject() {}
void pragma::physics::PhysXVehicle::DoAddWorldObject() {}
pragma::physics::PhysXEnvironment &pragma::physics::PhysXVehicle::GetPxEnv() const {return static_cast<PhysXEnvironment&>(m_physEnv);}

////////////////

pragma::physics::PhysXWheel &pragma::physics::PhysXWheel::GetWheel(IWheel &w)
{
	return *static_cast<PhysXWheel*>(w.GetUserData());
}
const pragma::physics::PhysXWheel &GetVehicle(const pragma::physics::IWheel &v) {return GetVehicle(const_cast<pragma::physics::IWheel&>(v));}

pragma::physics::PhysXWheel::PhysXWheel(IEnvironment &env)
	: IWheel{env}
{
	SetUserData(this);
}
void pragma::physics::PhysXWheel::Initialize()
{
	IWheel::Initialize();
	// TODO
	//GetInternalObject().userData = this;
}
pragma::physics::PhysXEnvironment &pragma::physics::PhysXWheel::GetPxEnv() const {return static_cast<PhysXEnvironment&>(m_physEnv);}
