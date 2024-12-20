/**
* @file FreeKickStrikerCard.cpp
* @author Flavio Volpi, Flavio Maiorana, Elisa Santini, Valerio Spagnoli
*/

#include "Tools/BehaviorControl/Framework/Card/Card.h"
#include "Tools/BehaviorControl/Framework/Card/CabslCard.h"
#include "Representations/BehaviorControl/Skills.h"

#include "Representations/Communication/TeamData.h"
#include "Representations/Communication/GameInfo.h"
#include "Representations/Communication/TeamInfo.h"
#include "Representations/Configuration/KickInfo.h"

#include "Representations/spqr_representations/GameState.h"
#include "Representations/BehaviorControl/FieldBall.h"
#include "Representations/Configuration/FieldDimensions.h"
#include "Representations/Modeling/RobotPose.h"
#include "Representations/Modeling/TeamBallModel.h"

#include "Representations/BehaviorControl/Libraries/LibStriker.h"
#include "Representations/BehaviorControl/Libraries/LibMisc.h"
#include "Representations/BehaviorControl/Libraries/LibSpec.h"
#include "Representations/BehaviorControl/Libraries/LibPass.h"
#include "Representations/BehaviorControl/Libraries/LibObstacles.h"

#include <iostream>
#include <cmath>

#include "Tools/Settings.h"

using Line2 = Eigen::Hyperplane<float,2>;


CARD(FreeKickStrikerCard,
{,
  CALLS(Activity),
  CALLS(GoToBallAndKick),
  CALLS(LookForward),
  CALLS(Stand),
  CALLS(WalkAtRelativeSpeed),
  CALLS(WalkToPoint),
  CALLS(LookAtBall),
  CALLS(GoToBallAndDribble),

  REQUIRES(GameInfo),
  REQUIRES(GameState),
  REQUIRES(FieldBall),  
  REQUIRES(TeamBallModel),
  REQUIRES(FieldDimensions),
  REQUIRES(RobotPose),
  REQUIRES(LibStriker),
  REQUIRES(LibMisc), 
  REQUIRES(LibSpec),  
  REQUIRES(LibObstacles),
  REQUIRES(TeamData),
  REQUIRES(LibPass),
  REQUIRES(OwnTeamInfo),

  DEFINES_PARAMETERS(
  {,
    (float)(0.8f) walkSpeed,
    (int)(100) initialWaitTime,
    (int)(7000) ballNotSeenTimeout,
    (int)(4500) time_when_last_seen,
  }),
});

class FreeKickStrikerCard : public FreeKickStrikerCardBase
{
  bool preconditions() const override
  {
     return (theGameInfo.state == STATE_PLAYING && 
            theGameInfo.setPlay != SET_PLAY_NONE) ||
            theGameInfo.gamePhase == GAME_PHASE_PENALTYSHOOT;
  }

  bool postconditions() const override
  {
     return theGameInfo.setPlay == SET_PLAY_NONE || theGameInfo.gamePhase != GAME_PHASE_PENALTYSHOOT;
  }

  Vector2f ballPositionAtLastTargetChoice;
  Vector2f MAGIC_VECTOR = Vector2f(-9999, -9999);


  option
  {
    theActivitySkill(BehaviorStatus::Striker);

    initial_state(start)
    {
      transition
      {
          if(theGameInfo.gamePhase == GAME_PHASE_PENALTYSHOOT && theGameInfo.kickingTeam == theOwnTeamInfo.teamNumber){
            goto ownPenaltyKick;
          }         
          switch(theGameState.state)
          {
            
            // ADV: Opponent Penalty Kick is 
            //      managed through OpponentKickOffCard
            
            case GameState::State::ownCorner:
              goto ownCorner; return;
            case GameState::State::opponentCorner:
              goto opponentCorner; return;
            case GameState::State::ownKickIn:
              goto ownKickIn; return;
            case GameState::State::opponentKickIn:
              goto opponentKickIn; return;
            case GameState::State::ownPenaltyKick:
              goto ownPenaltyKick; return;
            // case GameState::State::opponentPenaltyKick:
            //   goto opponentPenaltyKick; return;
            case GameState::State::ownPushingKick:
              goto ownPushingKick; return;
            case GameState::State::opponentPushingKick:
              goto opponentPushingKick; return;
            case GameState::State::ownGoalKick:
              goto ownGoalKick; return;
            case GameState::State::opponentGoalKick:
              goto opponentGoalKick; return;
          }
      }

      action
      {
        theLookForwardSkill();
        theStandSkill();
      }
    }

    state(opponentKickIn)
    {
      action
      {
        bool ballSeen = theFieldBall.ballWasSeen(time_when_last_seen);

        //Returns a position for the striker in a defensive kick in configuration
        Vector2f strikerPos = theLibStriker.strikerPosition;
        LocalPose2f position = theLibMisc.glob2Rel(strikerPos.x(),strikerPos.y());

        //Robot needs a pose, in order to orient the body towards the ball
        Pose2f targetPose = Pose2f(theLibMisc.angleToBall,position.translation);

        theLookAtBallSkill();
        theWalkToPointSkill(targetPose,1.0f);
      }  
    }

    state(ownKickIn)
    {
      action
      {
        ballPositionAtLastTargetChoice = MAGIC_VECTOR;
        
        if(theTeamData.numberOfActiveTeammates != 0 && 
           !(theTeamData.numberOfActiveTeammates == 1 && theLibSpec.isGoaliePlaying())
        ){
          Vector2f target = std::get<0>(theLibPass.getBestPassageSpecial());
          // std::cout << "x: " << std::to_string(target.x()) << "    y: "<< std::to_string(target.y()) << std::endl;

          float utility   = std::get<1>(theLibPass.getBestPassageSpecial());
          float distance = (theRobotPose.translation - target).norm();
          KickInfo::KickType kickType = theLibStriker.getWalkKick(target);

          theGoToBallAndKickSkill(theLibMisc.angleToTarget(target), kickType, true, distance);

        }
        else{
          ballPositionAtLastTargetChoice = MAGIC_VECTOR;
          theGoToBallAndDribbleSkill(theLibMisc.angleToTarget(theLibStriker.strikerDribblePoint));
        }
        
      }
    }

    state(opponentCorner)
    {
      action
      {
        bool ballSeen = theFieldBall.ballWasSeen(time_when_last_seen);
        Vector2f target_point = theLibMisc.glob2Rel(theLibStriker.strikerPosition.x(), theLibStriker.strikerPosition.y()).translation;
        theWalkToPointSkill(target_point);
        theLookAtBallSkill();
        /* ToDo
          rotazione striker
        */
      }
    }

    state(ownCorner)
    {
      action
      {
        float radius = 2500;
        std::tuple<float, float> angles = theLibSpec.calcAngleCorner();
        float angle_to_kick = std::get<0>(angles);
        float best_angle = std::get<1>(angles);
      
        if(best_angle<=0.34){ // 20° ToTest 
          //std::cout << best_angle << " --> BASE" << std::endl;
          (theFieldBall.recentBallPositionOnField().y() > 0) ? 
            theGoToBallAndKickSkill(theLibMisc.angleToTarget(Vector2f(theFieldDimensions.xPosOpponentGroundLine-1000, theFieldDimensions.yPosLeftSideline-1000)), KickInfo::walkForwardsRight, true, 600.0f, true, false): //ToTest the Length
            theGoToBallAndKickSkill(theLibMisc.angleToTarget(Vector2f(theFieldDimensions.xPosOpponentGroundLine-1000, theFieldDimensions.yPosRightSideline+1000)), KickInfo::walkForwardsRight, true, 600.0f, true, false); //ToTest the Length
        }
        else{
          //std::cout << best_angle << " --> STEP" << std::endl;
          (theFieldBall.recentBallPositionOnField().y() > 0) ? 
            theGoToBallAndKickSkill(theLibMisc.angleToTarget(theLibSpec.targetCornerPoint(angle_to_kick, radius)), KickInfo::forwardFastLeft, true, 3000.0f, true, false): //ToTest the length
            theGoToBallAndKickSkill(theLibMisc.angleToTarget(theLibSpec.targetCornerPoint(angle_to_kick, radius)), KickInfo::forwardFastRight, true, 3000.0f, true, false); //ToTest the length
        } 

        // bool isLeft = theFieldBall.recentBallPositionOnField().y() > 0;
        // Vector2f target = Vector2f(theFieldDimensions.xPosOpponentPenaltyMark+200, 0.f);
        // theGoToBallAndKickSkill(theLibMisc.angleToTarget(target), isLeft ? KickInfo::KickType::forwardFastLeft : KickInfo::KickType::forwardFastRight, true, 2500);
      }
    }

    // state(opponentPenaltyKick)
    // {
    //   action{
         
    //   }
    // }

    state(ownPenaltyKick)
    {
      action{
        // TODO: change target
        Vector2f target = Vector2f(theFieldDimensions.xPosOpponentGroundLine, 350.f);
        Angle angle = theLibMisc.angleToTarget(target);
        theGoToBallAndKickSkill(angle, KickInfo::KickType::forwardFastRight, true);
      }
    }

    state(opponentPushingKick)
    {
      
      action{
        bool ballSeen = theFieldBall.ballWasSeen(time_when_last_seen);
        Vector2f target_glob =  theLibStriker.strikerPosition;
        LocalPose2f target_loc = theLibMisc.glob2Rel(target_glob.x(), target_glob.y());
        theWalkToPointSkill(target_loc);
        theLookAtBallSkill();
      }
    }

    state(ownPushingKick)
    {
      action
      {
        ballPositionAtLastTargetChoice = MAGIC_VECTOR;
        
        if(theTeamData.numberOfActiveTeammates != 0 && 
           !(theTeamData.numberOfActiveTeammates == 1 && theLibSpec.isGoaliePlaying())
        ){
          Vector2f target = std::get<0>(theLibPass.getBestPassageSpecial());
          // std::cout << "x: " << std::to_string(target.x()) << "    y: "<< std::to_string(target.y()) << std::endl;

          float utility   = std::get<1>(theLibPass.getBestPassageSpecial());
          float distance = (theRobotPose.translation - target).norm();
          KickInfo::KickType kickType = theLibStriker.getWalkKick(target);

          theGoToBallAndKickSkill(theLibMisc.angleToTarget(target), kickType, true, distance);

        }
        else{
          ballPositionAtLastTargetChoice = MAGIC_VECTOR;
          theGoToBallAndDribbleSkill(theLibMisc.angleToTarget(theLibStriker.strikerDribblePoint));
        }
        
      }
    } 
    state(opponentGoalKick)
    {
      action
      {
        bool ballSeen = theFieldBall.ballWasSeen(time_when_last_seen);
        Vector2f target_point = theLibMisc.glob2Rel(theLibStriker.strikerPosition.x(), theLibStriker.strikerPosition.y()).translation;
        theWalkToPointSkill(target_point);
        theLookAtBallSkill();
        /* ToDo
          rotazione striker
        */
      }
    }
    state(ownGoalKick)
    {
      action
      {
        if(theLibSpec.isGoaliePlaying()){
          bool ballSeen = theFieldBall.ballWasSeen(time_when_last_seen);
          Vector2f target_point = theLibMisc.glob2Rel(theLibStriker.strikerPosition.x(), theLibStriker.strikerPosition.y()).translation;
          theWalkToPointSkill(target_point);
          theLookForwardSkill();
        }
        else{
          Vector2f leftEnd = Vector2f(0.f, -3000.f);
          Vector2f rightEnd = Vector2f(0.f, 3000.f);
          Vector2f target = theLibSpec.freeCorridor(leftEnd, rightEnd, 200.f);
          Angle direction = (theRobotPose.inversePose * target).angle();
          theGoToBallAndKickSkill(direction, KickInfo::KickType::forwardFastRightLong);
        }
      }
    }
  }

};

MAKE_CARD(FreeKickStrikerCard);