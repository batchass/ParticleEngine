//
//  Emitter.h
//  ParticleEngine
//
//  Created by Anthony Scavarelli on 2013-01-20.
//
//

#ifndef __FloatingCubes__EMITTER__
#define __FloatingCubes__EMITTER__

#include "cinder/app/AppBasic.h"
#include "cinder/Rand.h"
#include "cinder/Vector.h"
#include "cinder/Perlin.h"
#include "cinder/Camera.h"

#include <boost/thread.hpp>
#include <list>

#include "Particle.h"

using namespace ci;
using namespace ci::app;
using namespace std;

struct EmitterFormat
{
    public:
    
    EmitterFormat()
    {
        emitterPosition = Vec3f::zero();
		particlesPerSecond = 100; 
        
        particleRenderType = PARTICLE_RENDER_TYPE_BILLBOARD;
        particlePerlinType = PERLIN_TYPE_INDIVIDUAL;
		particleGravity = Vec3f::zero();
        particleSpeed = 0.3f;   
        particleColorA = ColorA(1.0f, 1.0f, 1.0f, 1.0f);
        particleSize = 5.0f;
        particleDecay = 0.99f;
        particleTurbulance = 0.0f;
        particleLifespanSeconds = 4.0f;
        
        isRandomSize = true;
        isRandomBrightness = false;
        isChangingOpacityOverTime = true;
        isChangingSizeOverTime = true;
        isChangingRotationOverTime = true;
        isImmortalParticle = false;
    }
    
    ~EmitterFormat(){}
    
    static const enum {
        PARTICLE_RENDER_TYPE_BILLBOARD,
        PARTICLE_RENDER_TYPE_QUAD,
        PARTICLE_RENDER_TYPE_POINT,
        PARTICLE_RENDER_TYPE_POINTSPRITE
    } PARTICLE_RENDER_TYPE;
    
    static const enum {
        PERLIN_TYPE_NONE,
        PERLIN_TYPE_INDIVIDUAL,
        PERLIN_TYPE_SYNCHRONIZED
    } PERLIN_TYPE;
    
	//emitter properties
    Vec3f   emitterPosition;
	int     particlesPerSecond;
    
    //particle properties
    int     particleRenderType;
    int     particlePerlinType;
	Vec3f   particleGravity;
    float   particleSpeed;
    ColorA  particleColorA;
    float   particleLifespanSeconds;
    float   particleSize;
    float   particleDecay;
    float   particleTurbulance;
    
    bool    isRandomSize;
    bool    isRandomBrightness;
    bool    isChangingOpacityOverTime;
    bool    isChangingSizeOverTime;
    bool    isChangingRotationOverTime;
    bool    isImmortalParticle;
};

template<typename EMITTER_TEMPLATE>
class Emitter
{
protected:
    list<EMITTER_TEMPLATE>      mParticles;
    list<EMITTER_TEMPLATE*>     mPurgParticles;
    int                         mCurrNumParticles;
    double                      mCurrTime;
    double                      mDiffTime;
    Perlin                      mPerlin;
    unsigned int                mCounter;
    
    boost::thread   mUpdateThread;
    boost::mutex    mWriteMutex;
    int             THREAD_UPDATE_INTERVAL_MILLISECONDS;
    bool            mIsThreadDead;
public:
	EmitterFormat               mFormat;
	int                         mMaxParticles;
    
public:
    Emitter(){}
    ~Emitter()
    {
        mIsThreadDead = true;
        mUpdateThread.interrupt();
        boost::this_thread::sleep(boost::posix_time::milliseconds(1000)); //need to give thread some time to interrupt before moving on ...
    }
    
    void setup( EmitterFormat format = EmitterFormat() )
    {
        mFormat = format;
        mCurrTime = getElapsedSeconds();
        mDiffTime = 0.0f;
        mMaxParticles = mFormat.particlesPerSecond * mFormat.particleLifespanSeconds + 2000;
        mCurrNumParticles = 0;
        
        //now start update thread
        THREAD_UPDATE_INTERVAL_MILLISECONDS = (int)((1.0f/getFrameRate()) * 1000.0f);
        boost::thread mUpdateThread( boost::bind( &Emitter::update, this ) );
        mIsThreadDead = false;
        
        mPerlin = Perlin(3);
        mPerlin.setSeed(getElapsedFrames() * randInt(1000));
        mCounter = 0.0f;
    }
    
    void setPosition( const Vec3f &newPos )
    {
		boost::mutex::scoped_lock lock(mWriteMutex, boost::try_to_lock);
        mFormat.emitterPosition = newPos;
    }
    
    Vec3f getPosition() const
    {
        return mFormat.emitterPosition;
    }

	int getNumLiveParticles() const
	{
		return mCurrNumParticles;
	}
    
    void render(const CameraPersp &cam)
    {
        for( typename list<EMITTER_TEMPLATE>::iterator p = mParticles.begin(); p != mParticles.end(); ++p ) {
            if( (!p->mIsDead) && (!p->mIsInPurgatory)) {
                p->render(&cam, mFormat.particleRenderType);
            }
        }
        
        gl::color(1.0f, 1.0f, 1.0f, 1.0f); //reset color for later use (just in case)
    }
    
    protected:
    inline void applyPerlin()
    {
        if (mFormat.particlePerlinType == EmitterFormat::PERLIN_TYPE_NONE) {
            //do nothing
        }
        else if(mFormat.particlePerlinType == EmitterFormat::PERLIN_TYPE_INDIVIDUAL) {
            for( typename list<EMITTER_TEMPLATE>::iterator p = mParticles.begin(); p != mParticles.end(); ++p ) {
                if ((!p->mIsDead) && (!p->mIsInPurgatory)) {
                    p->applyPerlin( mFormat.particleTurbulance );
                }
            }
        }
        else if(mFormat.particlePerlinType == EmitterFormat::PERLIN_TYPE_SYNCHRONIZED) {
            for( typename list<EMITTER_TEMPLATE>::iterator p = mParticles.begin(); p != mParticles.end(); ++p ) {
                if ((!p->mIsDead) && (!p->mIsInPurgatory)) {
                    Vec3f noiseVector = mPerlin.dfBm( Vec3f(0.0f, 0.0f, (float)mCounter) * mFormat.particleTurbulance ) * mFormat.particleTurbulance * 0.1f;
                    p->applyPerlin( mFormat.particleTurbulance, &noiseVector );
                }
            }
            
            mCounter++;
        }
    }
    
    void repulseParticles()
    {
        for( typename list<EMITTER_TEMPLATE>::iterator p1 = mParticles.begin(); p1 != mParticles.end(); ++p1 ) {
            typename list<EMITTER_TEMPLATE>::iterator p2 = p1;
            for( ++p2; p2 != mParticles.end(); ++p2 ) {
                Vec3f dir = p1->mLoc - p2->mLoc;
                
                float distSqrd = dir.lengthSquared();
                
                if( distSqrd > 0.0f ) {
                    float F = (1.0f/distSqrd) * 0.000001f;
                    dir.normalize();
                    
                    p1->mAcc += ( F * dir ) / 10.0f;
                    p2->mAcc -= ( F * dir ) / 10.0f;
                }
            }
        }
    }
    
    void update()
    {
        //console() << mParticles.size() << " " << mPurgParticles.size() << " " << mCurrNumParticles << " " << mMaxParticles << "\n";
        
        while(!mIsThreadDead)
        {
            boost::mutex::scoped_lock lock(mWriteMutex);
        
            mDiffTime = getElapsedSeconds() - mCurrTime;
            applyPerlin();
			//repulseParticles(); //soooo slow
            addParticles( mFormat.particlesPerSecond * mDiffTime );
        
            for( typename list<EMITTER_TEMPLATE>::iterator p = mParticles.begin(); p != mParticles.end(); ++p ) {
                if( p->mIsDead ) {
                    p->mIsDead = false;
                    p->mIsInPurgatory = true;
                    mPurgParticles.push_back( &*p );
                }
                else if (!p->mIsInPurgatory) {
                    p->update( mFormat.particleGravity );
                }
            }
            mCurrTime = getElapsedSeconds();
            
            boost::this_thread::sleep(boost::posix_time::milliseconds(THREAD_UPDATE_INTERVAL_MILLISECONDS));
        }
    }
    
    inline void addParticles( const int amt )
    {
        for( int i=0; i<amt; i++ ) {
            if( mCurrNumParticles < mMaxParticles ) {
                EMITTER_TEMPLATE newParticle = EMITTER_TEMPLATE();
                setupParticle( &newParticle );
                mParticles.push_back( newParticle );
                mCurrNumParticles++;
            }
            else {
                if( !mPurgParticles.empty() ) {
                    setupParticle( mPurgParticles.back() );
                    mPurgParticles.pop_back();
                }
            }
        }
    }
    
    inline void setupParticle(EMITTER_TEMPLATE *particle)
    {
        Vec3f loc = mFormat.emitterPosition;
        float randLifeSpan = Rand::randFloat( mFormat.particleLifespanSeconds * 0.95f, mFormat.particleLifespanSeconds * 1.05f );
        particle->setup(loc, mFormat.particleSpeed, mFormat.particleSize, mFormat.particleColorA, randLifeSpan, mFormat.particleDecay);
    }
};

#endif