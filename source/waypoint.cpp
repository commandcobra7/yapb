//
// Yet Another POD-Bot, based on PODBot by Markus Klinge ("CountFloyd").
// Copyright (c) YaPB Development Team.
//
// This software is licensed under the BSD-style license.
// Additional exceptions apply. For full license details, see LICENSE.txt or visit:
//     https://yapb.ru/license
//

#include <yapb.h>

ConVar yb_wptsubfolder ("yb_wptsubfolder", "");

ConVar yb_waypoint_autodl_host ("yb_waypoint_autodl_host", "yapb.ru");
ConVar yb_waypoint_autodl_enable ("yb_waypoint_autodl_enable", "1");

void Waypoint::init (void) {
   // this function initialize the waypoint structures..
   m_loadTries = 0;
   m_editFlags = 0;

   m_learnVelocity.nullify ();
   m_learnPosition.nullify ();
   m_lastWaypoint.nullify ();

   m_pathDisplayTime = 0.0f;
   m_arrowDisplayTime = 0.0f;
   m_autoPathDistance = 250.0f;

   // have any waypoint path nodes been allocated yet?
   if (m_waypointPaths) {
      cleanupPathMemory ();
   }

   // reset highest recorded damage
   for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
      m_highestDamage[team] = 1;
   }

   // free experience stuff
   delete[] m_experience;
   m_experience = nullptr;

   m_numWaypoints = 0;
}

void Waypoint::cleanupPathMemory (void) {
   for (int i = 0; i < m_numWaypoints && m_paths[i] != nullptr; i++) {
      delete m_paths[i];
      m_paths[i] = nullptr;
   }
}

int Waypoint::clearConnections (int index) {
   // this function removes the useless paths connections from and to waypoint pointed by index. This is based on code from POD-bot MM from KWo

   if (!exists (index)) {
      return 0;
   }
   int numConnectionsFixed = 0;

   if (bots.getBotCount () > 0) {
      bots.kickEveryone (true);
   }
   const int INFINITE_DISTANCE = 99999;

   struct Connection {
      int index;
      int number;
      int distance;
      float angles;

   public:
      Connection (void) {
         reset ();
      }

   public:
      void reset (void) {
         index = INVALID_WAYPOINT_INDEX;
         number = INVALID_WAYPOINT_INDEX;
         distance = INFINITE_DISTANCE;
         angles = 0.0f;
      }
   };

   Connection sorted[MAX_PATH_INDEX];
   Connection top;

   for (int i = 0; i < MAX_PATH_INDEX; i++) {
      auto &cur = sorted[i];

      cur.number = i;
      cur.index = m_paths[index]->index[i];
      cur.distance = m_paths[index]->distances[i];

      if (cur.index == INVALID_WAYPOINT_INDEX) {
         cur.distance = INFINITE_DISTANCE;
      }

      if (cur.distance < top.distance) {
         top.distance = m_paths[index]->distances[i];
         top.number = i;
         top.index = cur.index;
      }
   }

   if (top.number == INVALID_WAYPOINT_INDEX) {
      ctrl.msg ("Cannot find path to the closest connected waypoint to waypoint number %d!\n", index);
      return numConnectionsFixed;
   }
   bool sorting = false;

   // sort paths from the closest waypoint to the farest away one...
   do {
      sorting = false;

      for (int i = 0; i < MAX_PATH_INDEX - 1; i++) {
         if (sorted[i].distance > sorted[i + 1].distance) {
            cr::swap (sorted[i], sorted[i + 1]);
            sorting = true;
         }
      }
   } while (sorting);

   // calculate angles related to the angle of the closeset connected waypoint
   for (auto &cur : sorted) {
      if (cur.index == INVALID_WAYPOINT_INDEX) {
         cur.distance = INFINITE_DISTANCE;
         cur.angles = 360.0f;
      }
      else if (exists (cur.index)) {
         cur.angles = ((m_paths[cur.index]->origin - m_paths[index]->origin).toAngles () - (m_paths[sorted[0].index]->origin - m_paths[index]->origin).toAngles ()).y;

         if (cur.angles < 0.0f) {
            cur.angles += 360.0f;
         }
      }
   }

   //  sort the paths from the lowest to the highest angle (related to the vector closest waypoint - checked index)...
   do {
      sorting = false;

      for (int i = 0; i < MAX_PATH_INDEX - 1; i++) {
         if (sorted[i].index != INVALID_WAYPOINT_INDEX && sorted[i].angles > sorted[i + 1].angles) {
            cr::swap (sorted[i], sorted[i + 1]);
            sorting = true;
         }
      }
   } while (sorting);

   // reset top state
   top.reset ();

   auto unassignPath = [&](const int id1, const int id2) {
      m_waypointsChanged = true;

      m_paths[id1]->index[id2] = INVALID_WAYPOINT_INDEX;
      m_paths[id1]->distances[id2] = 0;
      m_paths[id1]->connectionFlags[id2] = 0;
      m_paths[id1]->connectionVelocity[id2].nullify ();

      m_waypointsChanged = true;
      setEditFlag (WS_EDIT_ENABLED);

      numConnectionsFixed++;
   };

   // check pass 0
   auto inspect_p0 = [&](const int id) -> bool  {
      if (id < 2) {
         return false;
      }
      auto &cur = sorted[id], &prev = sorted[id - 1], &prev2 = sorted[id - 2];

      if (cur.index == INVALID_WAYPOINT_INDEX || prev.index == INVALID_WAYPOINT_INDEX || prev2.index == INVALID_WAYPOINT_INDEX) {
         return false;
      }
      
      // store the highest index which should be tested later...
      top.index = cur.index;
      top.distance = cur.distance;
      top.angles = cur.angles;

      if (cur.angles - prev2.angles < 80.0f) {

         // leave alone ladder connections and don't remove jump connections..
         if (((m_paths[index]->flags & FLAG_LADDER) && (m_paths[prev.index]->flags & FLAG_LADDER)) || (m_paths[index]->connectionFlags[prev.number] & PATHFLAG_JUMP)) {
            return false;
         }

         if ((cur.distance + prev2.distance) * 1.1f / 2.0f < static_cast <float> (prev.distance)) {
            if (m_paths[index]->index[prev.number] == prev.index) {
               ctrl.msg ("Removing a useless (P.0.1) connection from index = %d to %d.", index, prev.index);

               // unassign this path
               unassignPath (index, prev.number);

               for (int j = 0; j < MAX_PATH_INDEX; j++) {
                  if (m_paths[prev.index]->index[j] == index && !(m_paths[prev.index]->connectionFlags[j] & PATHFLAG_JUMP)) {
                     ctrl.msg ("Removing a useless (P.0.2) connection from index = %d to %d.", prev.index, index);

                     // unassign this path
                     unassignPath (prev.index, j);
                  }
               }
               prev.index = INVALID_WAYPOINT_INDEX;

               for (int j = id - 1; j < MAX_PATH_INDEX - 1; j++) {
                  sorted[j] = cr::move (sorted[j + 1]);
               }
               sorted[MAX_PATH_INDEX - 1].index = INVALID_WAYPOINT_INDEX;

               // do a second check
               return true;
            }
            else {
               ctrl.msg ("Failed to remove a useless (P.0) connection from index = %d to %d.", index, prev.index);
               return false;
            }
         }
      }
      return false;
   };


   for (int i = 2; i < MAX_PATH_INDEX; i++) {
      while (inspect_p0 (i)) { }
   }

   // check pass 1
   if (exists (top.index) && exists (sorted[0].index) && exists (sorted[1].index)) {
      if ((sorted[1].angles - top.angles < 80.0f || 360.0f - (sorted[1].angles - top.angles) < 80.0f) && (!(m_paths[sorted[0].index]->flags & FLAG_LADDER) || !(m_paths[index]->flags & FLAG_LADDER)) && !(m_paths[index]->connectionFlags[sorted[0].number] & PATHFLAG_JUMP)) {
         if ((sorted[1].distance + top.distance) * 1.1f / 2.0f < static_cast <float> (sorted[0].distance)) {
            if (m_paths[index]->index[sorted[0].number] == sorted[0].index) {
               ctrl.msg ("Removing a useless (P.1.1) connection from index = %d to %d.", index, sorted[0].index);

               // unassign this path
               unassignPath (index, sorted[0].number);

               for (int j = 0; j < MAX_PATH_INDEX; j++) {
                  if (m_paths[sorted[0].index]->index[j] == index && !(m_paths[sorted[0].index]->connectionFlags[j] & PATHFLAG_JUMP)) {
                     ctrl.msg ("Removing a useless (P.1.2) connection from index = %d to %d.", sorted[0].index, index);

                     // unassign this path
                     unassignPath (sorted[0].index, j);
                  }
               }
               sorted[0].index = INVALID_WAYPOINT_INDEX;

               for (int j = 0; j < MAX_PATH_INDEX - 1; j++) {
                  sorted[j] = cr::move (sorted[j + 1]);
               }
               sorted[MAX_PATH_INDEX - 1].index = INVALID_WAYPOINT_INDEX;
            }
            else {
               ctrl.msg ("Failed to remove a useless (P.1) connection from index = %d to %d.", sorted[0].index, index);
            }
         }
      }
   }
   top.reset ();

   // check pass 2
   auto inspect_p2 = [&](const int id) -> bool {
      if (id < 1) {
         return false;
      }
      auto &cur = sorted[id], &prev = sorted[id - 1];

      if (cur.index == INVALID_WAYPOINT_INDEX || prev.index == INVALID_WAYPOINT_INDEX) {
         return false;
      }
      
      if (cur.angles - prev.angles < 40.0f) {
         if (prev.distance < static_cast <float> (cur.distance * 1.1f)) {

            // leave alone ladder connections and don't remove jump connections..
            if (((m_paths[index]->flags & FLAG_LADDER) && (m_paths[cur.index]->flags & FLAG_LADDER)) || (m_paths[index]->connectionFlags[cur.number] & PATHFLAG_JUMP)) {
               return false;
            }

            if (m_paths[index]->index[cur.number] == cur.index) {
               ctrl.msg ("Removing a useless (P.2.1) connection from index = %d to %d.", index, cur.index);

               // unassign this path
               unassignPath (index, cur.number);

               for (int j = 0; j < MAX_PATH_INDEX; j++) {
                  if (m_paths[cur.index]->index[j] == index && !(m_paths[cur.index]->connectionFlags[j] & PATHFLAG_JUMP)) {
                     ctrl.msg ("Removing a useless (P.2.2) connection from index = %d to %d.", cur.index, index);

                     // unassign this path
                     unassignPath (cur.index, j);
                  }
               }
               cur.index = INVALID_WAYPOINT_INDEX;

               for (int j = id - 1; j < MAX_PATH_INDEX - 1; j++) {
                  sorted[j] = cr::move (sorted[j + 1]);
               }
               sorted[MAX_PATH_INDEX - 1].index = INVALID_WAYPOINT_INDEX;
               return true;
            }
            else {
               ctrl.msg ("Failed to remove a useless (P.2) connection from index = %d to %d.", index, cur.index);
            }
         }
         else if (cur.distance < static_cast <float> (prev.distance * 1.1f)) {
            // leave alone ladder connections and don't remove jump connections..
            if (((m_paths[index]->flags & FLAG_LADDER) && (m_paths[prev.index]->flags & FLAG_LADDER)) || (m_paths[index]->connectionFlags[prev.number] & PATHFLAG_JUMP)) {
               return false;
            }

            if (m_paths[index]->index[prev.number] == prev.index) {
               ctrl.msg ("Removing a useless (P.2.3) connection from index = %d to %d.", index, prev.index);

               // unassign this path
               unassignPath (index, prev.number);

               for (int j = 0; j < MAX_PATH_INDEX; j++) {
                  if (m_paths[prev.index]->index[j] == index && !(m_paths[prev.index]->connectionFlags[j] & PATHFLAG_JUMP)) {
                     ctrl.msg ("Removing a useless (P.2.4) connection from index = %d to %d.", prev.index, index);

                     // unassign this path
                     unassignPath (prev.index, j);
                  }
               }
               prev.index = INVALID_WAYPOINT_INDEX;

               for (int j = id - 1; j < MAX_PATH_INDEX - 1; j++) {
                  sorted[j] = cr::move (sorted[j + 1]);
               }
               sorted[MAX_PATH_INDEX - 1].index = INVALID_WAYPOINT_INDEX;

               // do a second check
               return true;
            }
            else {
               ctrl.msg ("Failed to remove a useless (P.2) connection from index = %d to %d.", index, prev.index);
            }
         }
      }
      else {
         top = cur;
      }
      return false;
   };

   for (int i = 1; i < MAX_PATH_INDEX; i++) {
      while (inspect_p2 (i)) { }
   }

   // check pass 3
   if (exists (top.index) && exists (sorted[0].index)) {
      if ((top.angles - sorted[0].angles < 40.0f || (360.0f - top.angles - sorted[0].angles) < 40.0f) && (!(m_paths[sorted[0].index]->flags & FLAG_LADDER) || !(m_paths[index]->flags & FLAG_LADDER)) && !(m_paths[index]->connectionFlags[sorted[0].number] & PATHFLAG_JUMP)) {
         if (top.distance * 1.1f  < static_cast <float> (sorted[0].distance)) {
            if (m_paths[index]->index[sorted[0].number] == sorted[0].index) {
               ctrl.msg ("Removing a useless (P.3.1) connection from index = %d to %d.", index, sorted[0].index);

               // unassign this path
               unassignPath (index, sorted[0].number);

               for (int j = 0; j < MAX_PATH_INDEX; j++) {
                  if (m_paths[sorted[0].index]->index[j] == index && !(m_paths[sorted[0].index]->connectionFlags[j] & PATHFLAG_JUMP)) {
                     ctrl.msg ("Removing a useless (P.3.2) connection from index = %d to %d.", sorted[0].index, index);

                     // unassign this path
                     unassignPath (sorted[0].index, j);
                  }
               }
               sorted[0].index = INVALID_WAYPOINT_INDEX;

               for (int j = 0; j < MAX_PATH_INDEX - 1; j++) {
                  sorted[j] = cr::move (sorted[j + 1]);
               }
               sorted[MAX_PATH_INDEX - 1].index = INVALID_WAYPOINT_INDEX;
            }
            else {
               ctrl.msg ("Failed to remove a useless (P.3) connection from index = %d to %d.", sorted[0].index, index);
            }
         }
         else if (sorted[0].distance * 1.1f < static_cast <float> (top.distance) && !(m_paths[index]->connectionFlags[top.number] & PATHFLAG_JUMP)) {
            if (m_paths[index]->index[top.number] == top.index) {
               ctrl.msg ("Removing a useless (P.3.3) connection from index = %d to %d.", index, sorted[0].index);

               // unassign this path
               unassignPath (index, top.number);

               for (int j = 0; j < MAX_PATH_INDEX; j++) {
                  if (m_paths[top.index]->index[j] == index && !(m_paths[top.index]->connectionFlags[j] & PATHFLAG_JUMP)) {
                     ctrl.msg ("Removing a useless (P.3.4) connection from index = %d to %d.", sorted[0].index, index);

                     // unassign this path
                     unassignPath (top.index, j);
                  }
               }
               sorted[0].index = INVALID_WAYPOINT_INDEX;
            }
            else {
               ctrl.msg ("Failed to remove a useless (P.3) connection from index = %d to %d.", sorted[0].index, index);
            }
         }
      }
   }
   return numConnectionsFixed;
}

void Waypoint::addPath (int addIndex, int pathIndex, float distance) {
   if (!exists (addIndex) || !exists (pathIndex)) {
      return;
   }
   Path *path = m_paths[addIndex];

   // don't allow paths get connected twice
   for (auto &index : path->index) {
      if (index == pathIndex) {
         ctrl.msg ("Denied path creation from %d to %d (path already exists)", addIndex, pathIndex);
         return;
      }
   }

   // check for free space in the connection indices
   for (int i = 0; i < MAX_PATH_INDEX; i++) {
      if (path->index[i] == INVALID_WAYPOINT_INDEX) {
         path->index[i] = static_cast <int16> (pathIndex);
         path->distances[i] = cr::abs (static_cast <int> (distance));

         ctrl.msg ("Path added from %d to %d", addIndex, pathIndex);
         return;
      }
   }

   // there wasn't any free space. try exchanging it with a long-distance path
   int maxDistance = -9999;
   int slotID = INVALID_WAYPOINT_INDEX;

   for (int i = 0; i < MAX_PATH_INDEX; i++) {
      if (path->distances[i] > maxDistance) {
         maxDistance = path->distances[i];
         slotID = i;
      }
   }

   if (slotID != INVALID_WAYPOINT_INDEX) {
      ctrl.msg ("Path added from %d to %d", addIndex, pathIndex);

      path->index[slotID] = static_cast <int16> (pathIndex);
      path->distances[slotID] = cr::abs (static_cast <int> (distance));
   }
}

int Waypoint::getFarest (const Vector &origin, float maxDistance) {
   // find the farest waypoint to that Origin, and return the index to this waypoint

   int index = INVALID_WAYPOINT_INDEX;
   maxDistance = cr::square (maxDistance);

   for (int i = 0; i < m_numWaypoints; i++) {
      float distance = (m_paths[i]->origin - origin).lengthSq ();

      if (distance > maxDistance) {
         index = i;
         maxDistance = distance;
      }
   }
   return index;
}

int Waypoint::getNearestNoBuckets (const Vector &origin, float minDistance, int flags) {
   // find the nearest waypoint to that origin and return the index

   // fallback and go thru wall the waypoints...
   int index = INVALID_WAYPOINT_INDEX;
   minDistance = cr::square (minDistance);

   for (int i = 0; i < m_numWaypoints; i++) {
      if (flags != -1 && !(m_paths[i]->flags & flags)) {
         continue; // if flag not -1 and waypoint has no this flag, skip waypoint
      }
      float distance = (m_paths[i]->origin - origin).lengthSq ();

      if (distance < minDistance) {
         index = i;
         minDistance = distance;
      }
   }
   return index;
}

int Waypoint::getEditorNeareset (void) {
   if (!hasEditFlag (WS_EDIT_ENABLED)) {
      return INVALID_WAYPOINT_INDEX;
   }
   return getNearestNoBuckets (m_editor->v.origin, 50.0f);
}

int Waypoint::getNearest (const Vector &origin, float minDistance, int flags) {
   // find the nearest waypoint to that origin and return the index

   auto &bucket = getWaypointsInBucket (origin);

   if (bucket.empty ()) {
      return getNearestNoBuckets (origin, minDistance, flags);
   }
   int index = INVALID_WAYPOINT_INDEX;
   minDistance = cr::square (minDistance);

   for (const auto at : bucket) {
      if (flags != -1 && !(m_paths[at]->flags & flags)) {
         continue; // if flag not -1 and waypoint has no this flag, skip waypoint
      }
      float distance = (m_paths[at]->origin - origin).lengthSq ();

      if (distance < minDistance) {
         index = at;
         minDistance = distance;
      }
   }
   return index;
}

IntArray Waypoint::searchRadius (float radius, const Vector &origin, int maxCount) {
   // returns all waypoints within radius from position

   IntArray result;
   auto &bucket = getWaypointsInBucket (origin);

   if (bucket.empty ()) {
      result.push (getNearestNoBuckets (origin, radius));
      return cr::move (result);
   }
   radius = cr::square (radius);

   if (maxCount != -1) {
      result.reserve (maxCount);
   }

   for (const auto at : bucket) {
      if (maxCount != -1 && static_cast <int> (result.length ()) > maxCount) {
         break;
      }

      if ((m_paths[at]->origin - origin).lengthSq () < radius) {
         result.push (at);
      }
   }
   return cr::move (result);
}

void Waypoint::push (int flags, const Vector &waypointOrigin) {
   if (game.isNullEntity (m_editor)) {
      return;
   }

   int index = INVALID_WAYPOINT_INDEX, i;
   float distance;

   Vector forward;
   Path *path = nullptr;

   bool placeNew = true;
   Vector newOrigin = waypointOrigin;

   if (waypointOrigin.empty ()) {
      newOrigin = m_editor->v.origin;
   }

   if (bots.getBotCount () > 0) {
      bots.kickEveryone (true);
   }
   m_waypointsChanged = true;

   switch (flags) {
   case 6:
      index = getEditorNeareset ();

      if (index != INVALID_WAYPOINT_INDEX) {
         path = m_paths[index];

         if (!(path->flags & FLAG_CAMP)) {
            ctrl.msg ("This is not Camping Waypoint");
            return;
         }
         game.makeVectors (m_editor->v.v_angle);
         forward = m_editor->v.origin + m_editor->v.view_ofs + game.vec.forward * 640.0f;

         path->campEndX = forward.x;
         path->campEndY = forward.y;

         // play "done" sound...
         game.playSound (m_editor, "common/wpn_hudon.wav");
      }
      return;

   case 9:
      index = getEditorNeareset ();

      if (index != INVALID_WAYPOINT_INDEX && m_paths[index] != nullptr) {
         distance = (m_paths[index]->origin - m_editor->v.origin).length ();

         if (distance < 50.0f) {
            placeNew = false;

            path = m_paths[index];
            path->origin = (path->origin + m_learnPosition) * 0.5f;
         }
      }
      else {
         newOrigin = m_learnPosition;
      }
      break;

   case 10:
      index = getEditorNeareset ();

      if (index != INVALID_WAYPOINT_INDEX && m_paths[index] != nullptr) {
         distance = (m_paths[index]->origin - m_editor->v.origin).length ();

         if (distance < 50.0f) {
            placeNew = false;
            path = m_paths[index];

            int connectionFlags = 0;

            for (i = 0; i < MAX_PATH_INDEX; i++) {
               connectionFlags += path->connectionFlags[i];
            }
            if (connectionFlags == 0) {
               path->origin = (path->origin + m_editor->v.origin) * 0.5f;
            }
         }
      }
      break;
   }

   if (placeNew) {
      if (m_numWaypoints >= MAX_WAYPOINTS) {
         return;
      }
      index = m_numWaypoints;

      m_paths[index] = new Path;
      path = m_paths[index];

      // increment total number of waypoints
      m_numWaypoints++;
      path->pathNumber = index;
      path->flags = 0;

      // store the origin (location) of this waypoint
      path->origin = newOrigin;
      addToBucket (newOrigin, index);

      path->campEndX = 0.0f;
      path->campEndY = 0.0f;
      path->campStartX = 0.0f;
      path->campStartY = 0.0f;

      for (i = 0; i < MAX_PATH_INDEX; i++) {
         path->index[i] = INVALID_WAYPOINT_INDEX;
         path->distances[i] = 0;

         path->connectionFlags[i] = 0;
         path->connectionVelocity[i].nullify ();
      }

      // store the last used waypoint for the auto waypoint code...
      m_lastWaypoint = m_editor->v.origin;
   }

   // set the time that this waypoint was originally displayed...
   m_waypointDisplayTime[index] = 0;

   if (flags == 9) {
      m_lastJumpWaypoint = index;
   }
   else if (flags == 10) {
      distance = (m_paths[m_lastJumpWaypoint]->origin - m_editor->v.origin).length ();
      addPath (m_lastJumpWaypoint, index, distance);

      for (i = 0; i < MAX_PATH_INDEX; i++) {
         if (m_paths[m_lastJumpWaypoint]->index[i] == index) {
            m_paths[m_lastJumpWaypoint]->connectionFlags[i] |= PATHFLAG_JUMP;
            m_paths[m_lastJumpWaypoint]->connectionVelocity[i] = m_learnVelocity;

            break;
         }
      }

      calculatePathRadius (index);
      return;
   }

   if (path == nullptr) {
      return;
   }

   if (m_editor->v.flags & FL_DUCKING) {
      path->flags |= FLAG_CROUCH; // set a crouch waypoint
   }

   if (m_editor->v.movetype == MOVETYPE_FLY) {
      path->flags |= FLAG_LADDER;
      game.makeVectors (m_editor->v.v_angle);

      forward = m_editor->v.origin + m_editor->v.view_ofs + game.vec.forward * 640.0f;
      path->campStartY = forward.y;
   }
   else if (m_isOnLadder) {
      path->flags |= FLAG_LADDER;
   }

   switch (flags) {
   case 1:
      path->flags |= FLAG_CROSSING;
      path->flags |= FLAG_TF_ONLY;
      break;

   case 2:
      path->flags |= FLAG_CROSSING;
      path->flags |= FLAG_CF_ONLY;
      break;

   case 3:
      path->flags |= FLAG_NOHOSTAGE;
      break;

   case 4:
      path->flags |= FLAG_RESCUE;
      break;

   case 5:
      path->flags |= FLAG_CROSSING;
      path->flags |= FLAG_CAMP;

      game.makeVectors (m_editor->v.v_angle);
      forward = m_editor->v.origin + m_editor->v.view_ofs + game.vec.forward * 640.0f;

      path->campStartX = forward.x;
      path->campStartY = forward.y;
      break;

   case 100:
      path->flags |= FLAG_GOAL;
      break;
   }

   // Ladder waypoints need careful connections
   if (path->flags & FLAG_LADDER) {
      float minDistance = 9999.0f;
      int destIndex = INVALID_WAYPOINT_INDEX;

      TraceResult tr;

      // calculate all the paths to this new waypoint
      for (i = 0; i < m_numWaypoints; i++) {
         if (i == index) {
            continue; // skip the waypoint that was just added
         }

         // other ladder waypoints should connect to this
         if (m_paths[i]->flags & FLAG_LADDER) {
            // check if the waypoint is reachable from the new one
            game.testLine (newOrigin, m_paths[i]->origin, TRACE_IGNORE_MONSTERS, m_editor, &tr);

            if (tr.flFraction == 1.0f && cr::abs (newOrigin.x - m_paths[i]->origin.x) < 64.0f && cr::abs (newOrigin.y - m_paths[i]->origin.y) < 64.0f && cr::abs (newOrigin.z - m_paths[i]->origin.z) < m_autoPathDistance) {
               distance = (m_paths[i]->origin - newOrigin).length ();

               addPath (index, i, distance);
               addPath (i, index, distance);
            }
         }
         else {
            // check if the waypoint is reachable from the new one
            if (isNodeReacheable (newOrigin, m_paths[i]->origin) || isNodeReacheable (m_paths[i]->origin, newOrigin)) {
               distance = (m_paths[i]->origin - newOrigin).length ();

               if (distance < minDistance) {
                  destIndex = i;
                  minDistance = distance;
               }
            }
         }
      }

      if (exists (destIndex)) {
         // check if the waypoint is reachable from the new one (one-way)
         if (isNodeReacheable (newOrigin, m_paths[destIndex]->origin)) {
            distance = (m_paths[destIndex]->origin - newOrigin).length ();
            addPath (index, destIndex, distance);
         }

         // check if the new one is reachable from the waypoint (other way)
         if (isNodeReacheable (m_paths[destIndex]->origin, newOrigin)) {
            distance = (m_paths[destIndex]->origin - newOrigin).length ();
            addPath (destIndex, index, distance);
         }
      }
   }
   else {
      // calculate all the paths to this new waypoint
      for (i = 0; i < m_numWaypoints; i++) {
         if (i == index) {
            continue; // skip the waypoint that was just added
         }

         // check if the waypoint is reachable from the new one (one-way)
         if (isNodeReacheable (newOrigin, m_paths[i]->origin)) {
            distance = (m_paths[i]->origin - newOrigin).length ();
            addPath (index, i, distance);
         }

         // check if the new one is reachable from the waypoint (other way)
         if (isNodeReacheable (m_paths[i]->origin, newOrigin)) {
            distance = (m_paths[i]->origin - newOrigin).length ();
            addPath (i, index, distance);
         }
      }
      clearConnections (index);
   }
   game.playSound (m_editor, "weapons/xbow_hit1.wav");
   calculatePathRadius (index); // calculate the wayzone of this waypoint
}

void Waypoint::erase (int target) {
   m_waypointsChanged = true;

   if (m_numWaypoints < 1) {
      return;
   }

   if (bots.getBotCount () > 0) {
      bots.kickEveryone (true);
   }
   int index = (target == INVALID_WAYPOINT_INDEX) ? getEditorNeareset () : target;

   if (!exists (index)) {
      return;
   }

   Path *path = nullptr;
   assert (m_paths[index] != nullptr);

   int i, j;

   for (i = 0; i < m_numWaypoints; i++) // delete all references to Node
   {
      path = m_paths[i];

      for (j = 0; j < MAX_PATH_INDEX; j++) {
         if (path->index[j] == index) {
            path->index[j] = INVALID_WAYPOINT_INDEX; // unassign this path
            path->connectionFlags[j] = 0;
            path->distances[j] = 0;
            path->connectionVelocity[j].nullify ();
         }
      }
   }

   for (i = 0; i < m_numWaypoints; i++) {
      path = m_paths[i];

      if (path->pathNumber > index) { // if pathnumber bigger than deleted node...
         path->pathNumber--;
      }

      for (j = 0; j < MAX_PATH_INDEX; j++) {
         if (path->index[j] > index) {
            path->index[j]--;
         }
      }
   }
   eraseFromBucket (m_paths[index]->origin, index);

   // free deleted node
   delete m_paths[index];
   m_paths[index] = nullptr;

   // rotate path array down
   for (i = index; i < m_numWaypoints - 1; i++) {
      m_paths[i] = m_paths[i + 1];
   }
   m_numWaypoints--;
   m_waypointDisplayTime[index] = 0;

   game.playSound (m_editor, "weapons/mine_activate.wav");
}

void Waypoint::toggleFlags (int toggleFlag) {
   // this function allow manually changing flags

   int index = getEditorNeareset ();

   if (index != INVALID_WAYPOINT_INDEX) {
      if (m_paths[index]->flags & toggleFlag) {
         m_paths[index]->flags &= ~toggleFlag;
      }
      else if (!(m_paths[index]->flags & toggleFlag)) {
         if (toggleFlag == FLAG_SNIPER && !(m_paths[index]->flags & FLAG_CAMP)) {
            ctrl.msg ("Cannot assign sniper flag to waypoint #%d. This is not camp waypoint", index);
            return;
         }
         m_paths[index]->flags |= toggleFlag;
      }

      // play "done" sound...
      game.playSound (m_editor, "common/wpn_hudon.wav");
   }
}

void Waypoint::setRadius (int index, float radius) {
   // this function allow manually setting the zone radius

   int node = exists (index) ? index : getEditorNeareset ();

   if (node != INVALID_WAYPOINT_INDEX) {
      m_paths[node]->radius = static_cast <float> (radius);

      // play "done" sound...
      game.playSound (m_editor, "common/wpn_hudon.wav");
   }
}

bool Waypoint::isConnected (int pointA, int pointB) {
   // this function checks if waypoint A has a connection to waypoint B

   for (auto &index : m_paths[pointA]->index) {
      if (index == pointB) {
         return true;
      }
   }
   return false;
}

int Waypoint::getFacingIndex (void) {
   // this function finds waypoint the user is pointing at.

   int indexToPoint = INVALID_WAYPOINT_INDEX;

   Array <float> cones;
   float maxCone = 0.0f;

   // find the waypoint the user is pointing at
   for (int i = 0; i < m_numWaypoints; i++) {
      auto path = m_paths[i];

      if ((path->origin - m_editor->v.origin).lengthSq () > cr::square (500.0f)) {
         continue;
      }
      cones.clear ();

      // get the current view cones
      cones.push (util.getShootingCone (m_editor, path->origin));
      cones.push (util.getShootingCone (m_editor, path->origin - Vector (0.0f, 0.0f, (path->flags & FLAG_CROUCH) ? 6.0f : 12.0f)));
      cones.push (util.getShootingCone (m_editor, path->origin - Vector (0.0f, 0.0f, (path->flags & FLAG_CROUCH) ? 12.0f : 24.0f)));
      cones.push (util.getShootingCone (m_editor, path->origin + Vector (0.0f, 0.0f, (path->flags & FLAG_CROUCH) ? 6.0f : 12.0f)));
      cones.push (util.getShootingCone (m_editor, path->origin + Vector (0.0f, 0.0f, (path->flags & FLAG_CROUCH) ? 12.0f : 24.0f)));

      // check if we can see it
      for (auto &cone : cones) {
         if (cone > 1.000f && cone > maxCone) {
            maxCone = cone;
            indexToPoint = i;
         }
      }
   }
   return indexToPoint;
}

void Waypoint::pathCreate (char dir) {
   // this function allow player to manually create a path from one waypoint to another

   int nodeFrom = getEditorNeareset ();

   if (nodeFrom == INVALID_WAYPOINT_INDEX) {
      ctrl.msg ("Unable to find nearest waypoint in 50 units");
      return;
   }
   int nodeTo = m_facingAtIndex;

   if (!exists (nodeTo)) {
      if (exists (m_cacheWaypointIndex)) {
         nodeTo = m_cacheWaypointIndex;
      }
      else {
         ctrl.msg ("Unable to find destination waypoint");
         return;
      }
   }

   if (nodeTo == nodeFrom) {
      ctrl.msg ("Unable to connect waypoint with itself");
      return;
   }

   float distance = (m_paths[nodeTo]->origin - m_paths[nodeFrom]->origin).length ();

   if (dir == CONNECTION_OUTGOING) {
      addPath (nodeFrom, nodeTo, distance);
   }
   else if (dir == CONNECTION_INCOMING) {
      addPath (nodeTo, nodeFrom, distance);
   }
   else {
      addPath (nodeFrom, nodeTo, distance);
      addPath (nodeTo, nodeFrom, distance);
   }

   game.playSound (m_editor, "common/wpn_hudon.wav");
   m_waypointsChanged = true;
}

void Waypoint::erasePath (void) {
   // this function allow player to manually remove a path from one waypoint to another

   int nodeFrom = getEditorNeareset ();
   int index = 0;

   if (nodeFrom == INVALID_WAYPOINT_INDEX) {
      ctrl.msg ("Unable to find nearest waypoint in 50 units");
      return;
   }
   int nodeTo = m_facingAtIndex;

   if (!exists (nodeTo)) {
      if (exists (m_cacheWaypointIndex)) {
         nodeTo = m_cacheWaypointIndex;
      }
      else {
         ctrl.msg ("Unable to find destination waypoint");
         return;
      }
   }

   for (index = 0; index < MAX_PATH_INDEX; index++) {
      if (m_paths[nodeFrom]->index[index] == nodeTo) {
         m_waypointsChanged = true;

         m_paths[nodeFrom]->index[index] = INVALID_WAYPOINT_INDEX; // unassigns this path
         m_paths[nodeFrom]->distances[index] = 0;
         m_paths[nodeFrom]->connectionFlags[index] = 0;
         m_paths[nodeFrom]->connectionVelocity[index].nullify ();

         game.playSound (m_editor, "weapons/mine_activate.wav");
         return;
      }
   }

   // not found this way ? check for incoming connections then
   index = nodeFrom;
   nodeFrom = nodeTo;
   nodeTo = index;

   for (index = 0; index < MAX_PATH_INDEX; index++) {
      if (m_paths[nodeFrom]->index[index] == nodeTo) {
         m_waypointsChanged = true;

         m_paths[nodeFrom]->index[index] = INVALID_WAYPOINT_INDEX; // unassign this path
         m_paths[nodeFrom]->distances[index] = 0;

         m_paths[nodeFrom]->connectionFlags[index] = 0;
         m_paths[nodeFrom]->connectionVelocity[index].nullify ();

         game.playSound (m_editor, "weapons/mine_activate.wav");
         return;
      }
   }
   ctrl.msg ("There is already no path on this waypoint");
}

void Waypoint::cachePoint (int index) {
   int node = exists (index) ? index : getEditorNeareset ();

   if (node == INVALID_WAYPOINT_INDEX) {
      m_cacheWaypointIndex = INVALID_WAYPOINT_INDEX;
      ctrl.msg ("Cached waypoint cleared (nearby point not found in 50 units range)");

      return;
   }
   m_cacheWaypointIndex = node;
   ctrl.msg ("Waypoint #%d has been put into memory", m_cacheWaypointIndex);
}

void Waypoint::calculatePathRadius (int index) {
   // calculate "wayzones" for the nearest waypoint to pentedict (meaning a dynamic distance area to vary waypoint origin)

   Path *path = m_paths[index];
   Vector start, direction;

   if ((path->flags & (FLAG_LADDER | FLAG_GOAL | FLAG_CAMP | FLAG_RESCUE | FLAG_CROUCH)) || m_learnJumpWaypoint) {
      path->radius = 0.0f;
      return;
   }

   for (auto &test : path->index) {
      if (test != INVALID_WAYPOINT_INDEX && (m_paths[test]->flags & FLAG_LADDER)) {
         path->radius = 0.0f;
         return;
      }
   }
   TraceResult tr;
   bool wayBlocked = false;

   for (float scanDistance = 32.0f; scanDistance < 128.0f; scanDistance += 16.0f) {
      start = path->origin;
      game.makeVectors (Vector::null ());

      direction = game.vec.forward * scanDistance;
      direction = direction.toAngles ();

      path->radius = scanDistance;

      for (float circleRadius = 0.0f; circleRadius < 360.0f; circleRadius += 20.0f) {
         game.makeVectors (direction);

         Vector radiusStart = start + game.vec.forward * scanDistance;
         Vector radiusEnd = start + game.vec.forward * scanDistance;

         game.testHull (radiusStart, radiusEnd, TRACE_IGNORE_MONSTERS, head_hull, nullptr, &tr);

         if (tr.flFraction < 1.0f) {
            game.testLine (radiusStart, radiusEnd, TRACE_IGNORE_MONSTERS, nullptr, &tr);

            if (strncmp ("func_door", STRING (tr.pHit->v.classname), 9) == 0) {
               path->radius = 0.0f;
               wayBlocked = true;

               break;
            }
            wayBlocked = true;
            path->radius -= 16.0f;

            break;
         }

         Vector dropStart = start + game.vec.forward * scanDistance;
         Vector dropEnd = dropStart - Vector (0.0f, 0.0f, scanDistance + 60.0f);

         game.testHull (dropStart, dropEnd, TRACE_IGNORE_MONSTERS, head_hull, nullptr, &tr);

         if (tr.flFraction >= 1.0f) {
            wayBlocked = true;
            path->radius -= 16.0f;

            break;
         }
         dropStart = start - game.vec.forward * scanDistance;
         dropEnd = dropStart - Vector (0.0f, 0.0f, scanDistance + 60.0f);

         game.testHull (dropStart, dropEnd, TRACE_IGNORE_MONSTERS, head_hull, nullptr, &tr);

         if (tr.flFraction >= 1.0f) {
            wayBlocked = true;
            path->radius -= 16.0f;
            break;
         }

         radiusEnd.z += 34.0f;
         game.testHull (radiusStart, radiusEnd, TRACE_IGNORE_MONSTERS, head_hull, nullptr, &tr);

         if (tr.flFraction < 1.0f) {
            wayBlocked = true;
            path->radius -= 16.0f;

            break;
         }
         direction.y = cr::angleNorm (direction.y + circleRadius);
      }

      if (wayBlocked) {
         break;
      }
   }
   path->radius -= 16.0f;

   if (path->radius < 0.0f) {
      path->radius = 0.0f;
   }
}

void Waypoint::saveExperience (void) {
   if (m_numWaypoints < 1 || m_waypointsChanged) {
      return;
   }
   saveExtFile ("exp", "Experience", FH_EXPERIENCE, FV_EXPERIENCE, reinterpret_cast <uint8 *> (m_experience), m_numWaypoints * m_numWaypoints * sizeof (Experience));
}

void Waypoint::loadExperience (void) {
   delete[] m_experience;
   m_experience = nullptr;

   if (m_numWaypoints < 1) {
      return;
   }
   m_experience = new Experience[m_numWaypoints * m_numWaypoints + FastLZ::EXCESS];

   // reset highest recorded damage
   for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
      m_highestDamage[team] = 1;
   }

   // initialize table by hand to correct values, and NOT zero it out
   for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
      for (int i = 0; i < m_numWaypoints; i++) {
         for (int j = 0; j < m_numWaypoints; j++) {
            (m_experience + (i * m_numWaypoints) + j)->index[team] = INVALID_WAYPOINT_INDEX;
            (m_experience + (i * m_numWaypoints) + j)->damage[team] = 0;
            (m_experience + (i * m_numWaypoints) + j)->value[team] = 0;
         }
      }
   }
   bool isLoaded = loadExtFile ("exp", "Experience", FH_EXPERIENCE, FV_EXPERIENCE, reinterpret_cast <uint8 *> (m_experience));

   // set's the highest damage if loaded ok
   if (!isLoaded) {
      return;
   }

   for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
      for (int i = 0; i < m_numWaypoints; i++) {
         for (int j = 0; j < m_numWaypoints; j++) {
            if (i == j) {
               if ((m_experience + (i * m_numWaypoints) + j)->damage[team] > m_highestDamage[team]) {
                  m_highestDamage[team] = (m_experience + (i * m_numWaypoints) + j)->damage[team];
               }
            }
         }
      }
   }
}

void Waypoint::loadVisibility (void) {
   m_visibilityIndex = 0;
   m_needsVisRebuild = true;

   if (m_numWaypoints <= 0) {
      return;
   }
   bool isLoaded = loadExtFile ("vis", "Visibility", FH_VISTABLE, FV_VISTABLE, reinterpret_cast <uint8 *> (m_visLUT));

   // if loaded, do not recalculate visibility
   if (isLoaded) {
      m_needsVisRebuild = false;
   }
}

void Waypoint::saveVisibility (void) {
   if (m_numWaypoints < 1 || m_waypointsChanged) {
      return;
   }
   saveExtFile ("vis", "Visibility", FH_VISTABLE, FV_VISTABLE, reinterpret_cast <uint8 *> (m_visLUT), MAX_WAYPOINTS * (MAX_WAYPOINTS / 4) * sizeof (uint8));
}

void Waypoint::savePathMatrix (void) {
   if (m_numWaypoints < 1) {
      return;
   }
   saveExtFile ("pmx", "Pathmatrix", FH_MATRIX, FV_MATRIX, reinterpret_cast <uint8 *> (m_matrix), m_numWaypoints * m_numWaypoints * sizeof (FloydMatrix));
}

bool Waypoint::loadPathMatrix (void) {
   if (m_numWaypoints <= 0) {
      return false;
   }
   return loadExtFile ("pmx", "Pathmatrix", FH_MATRIX, FV_MATRIX, reinterpret_cast <uint8 *> (m_matrix));
}

bool Waypoint::saveExtFile (const char *ext, const char *type, const char *magic, int version, uint8 *data, int32 size) {
   FastLZ lz;
   bool dataSaved = false;

   ExtHeader header;
   header.pointNumber = m_numWaypoints;
   header.fileVersion = version;
   header.uncompressed = size;

   strncpy (header.header, magic, cr::bufsize (header.header));

   auto compressed = new uint8[header.uncompressed];
   int compressedLength = lz.compress (reinterpret_cast <uint8 *> (data), header.uncompressed, compressed);

   if (compressedLength > 0) {
      File fp (util.format ("%slearned/%s.%s", getDataDirectory (), game.getMapName (), ext), "wb");

      if (fp.isValid ()) {
         header.compressed = compressedLength;

         fp.write (&header, sizeof (ExtHeader));
         fp.write (compressed, compressedLength);
         fp.close ();

         game.print ("Successfully saved Bots %s data.", type);
         dataSaved = true;
      }
      else {
         util.logEntry (true, LL_ERROR, "Couldn't save %s data (unable to write the file \"%s\")", type, util.format ("%slearned/%s.%s", getDataDirectory (), game.getMapName (), ext));
         dataSaved = false;
      }
   }
   else {
      util.logEntry (true, LL_ERROR, "Couldn't save %s data (unable to compress data)", type);
      dataSaved = false;
   }
   delete[] compressed;

   return dataSaved;
}

bool Waypoint::loadExtFile (const char *ext, const char *type, const char *magic, int version, uint8 *data) {
   File fp (util.format ("%slearned/%s.%s", getDataDirectory (), game.getMapName (), ext), "rb");

   // if file exists, read the visibility data from it
   if (!fp.isValid ()) {
      return false;
   }
   ExtHeader header;

   if (fp.read (&header, sizeof (ExtHeader)) == 0) {
      util.logEntry (true, LL_ERROR, "%s data damaged (unable to read header)", type);
      fp.close ();

      return false;
   }

   if (!!strncmp (header.header, magic, cr::bufsize (header.header))) {
      util.logEntry (true, LL_ERROR, "%s data damaged (bad header '%s')", type, header.header);
      fp.close ();

      return false;
   }

   FastLZ lz;
   bool dataLoaded = false;

   // check the header
   if (header.fileVersion == version && header.pointNumber == m_numWaypoints && header.compressed > 1) {
      auto compressed = new uint8[header.compressed];

      if (fp.read (compressed, sizeof (uint8), header.compressed) == static_cast <size_t> (header.compressed)) {
         int status = lz.uncompress (compressed, header.compressed, data, header.uncompressed);

         if (status == FastLZ::UNCOMPRESS_RESULT_FAILED) {
            util.logEntry (true, LL_ERROR, "%s data damaged (failed to decompress data)", type);
            dataLoaded = false;
         }
         else {
            game.print ("Successfully loaded the bots %s tables.", type);
            dataLoaded = true;
         }
      }
      else {
         util.logEntry (true, LL_ERROR, "%s data damaged (unable to read compressed data)", type);
         dataLoaded = false;
      }
      delete[] compressed;
   }
   else {
      util.logEntry (true, LL_ERROR, "%s data damaged (wrong version, or not for this map)", type);
      dataLoaded = false;
   }
   fp.close ();

   return dataLoaded;
}

void Waypoint::initLightLevels (void) {
   // this function get's the light level for each waypoin on the map

   // no waypoints ? no light levels, and only one-time init
   if (!m_numWaypoints || !cr::fzero (m_waypointLightLevel[0])) {
      return;
   }

   // update light levels for all waypoints
   for (int i = 0; i < m_numWaypoints; i++) {
      m_waypointLightLevel[i] = illum.getLightLevel (m_paths[i]->origin);
   }
   // disable lightstyle animations on finish (will be auto-enabled on mapchange)
   illum.enableAnimation (false);
}

void Waypoint::initTypes (void) {
   m_terrorPoints.clear ();
   m_ctPoints.clear ();
   m_goalPoints.clear ();
   m_campPoints.clear ();
   m_rescuePoints.clear ();
   m_sniperPoints.clear ();
   m_visitedGoals.clear ();

   for (int i = 0; i < m_numWaypoints; i++) {
      if (m_paths[i]->flags & FLAG_TF_ONLY) {
         m_terrorPoints.push (i);
      }
      else if (m_paths[i]->flags & FLAG_CF_ONLY) {
         m_ctPoints.push (i);
      }
      else if (m_paths[i]->flags & FLAG_GOAL) {
         m_goalPoints.push (i);
      }
      else if (m_paths[i]->flags & FLAG_CAMP) {
         m_campPoints.push (i);
      }
      else if (m_paths[i]->flags & FLAG_SNIPER) {
         m_sniperPoints.push (i);
      }
      else if (m_paths[i]->flags & FLAG_RESCUE) {
         m_rescuePoints.push (i);
      }
   }
}

bool Waypoint::load (void) {
   initBuckets ();

   if (m_loadTries++ > 3) {
      m_loadTries = 0;

      m_tempInfo.assign ("Giving up loading waypoint file (%s). Something went wrong.", game.getMapName ());
      util.logEntry (true, LL_ERROR, m_tempInfo.chars ());

      return false;
   }
   MemFile fp (getWaypointFilename (true));
 
   WaypointHeader header;
   memset (&header, 0, sizeof (header));

   // save for faster access
   const char *map = game.getMapName ();

   // helper function
   auto throwError = [&] (const char *fmt, ...) -> bool {
      char infobuffer[MAX_PRINT_BUFFER];

      va_list ap;
      va_start (ap, fmt);
      vsnprintf (infobuffer, MAX_PRINT_BUFFER - 1, fmt, ap);
      va_end (ap);

      util.logEntry (true, LL_ERROR, infobuffer);
      ctrl.msg (infobuffer);

      m_tempInfo = infobuffer;

      if (fp.isValid ()) {
         fp.close ();
      }
      m_numWaypoints = 0;
      m_waypointPaths = false;

      return false;
   };

   if (fp.isValid ()) {
      if (fp.read (&header, sizeof (header)) == 0) {
         return throwError ("%s.pwf - damaged waypoint file (unable to read header)", map);
      }

      if (strncmp (header.header, FH_WAYPOINT, cr::bufsize (FH_WAYPOINT)) == 0) {
         if (header.fileVersion != FV_WAYPOINT) {
            return throwError ("%s.pwf - incorrect waypoint file version (expected '%d' found '%ld')", map, FV_WAYPOINT, header.fileVersion);
         }
         else if (!!stricmp (header.mapName, map)) {
            return throwError ("%s.pwf - hacked waypoint file, file name doesn't match waypoint header information (mapname: '%s', header: '%s')", map, map, header.mapName);
         }
         else {
            if (header.pointNumber == 0 || header.pointNumber > MAX_WAYPOINTS) {
               return throwError ("%s.pwf - waypoint file contains illegal number of waypoints (mapname: '%s', header: '%s')", map, map, header.mapName);
            }

            init ();
            m_numWaypoints = header.pointNumber;

            for (int i = 0; i < m_numWaypoints; i++) {
               m_paths[i] = new Path;

               if (fp.read (m_paths[i], sizeof (Path)) == 0) {
                  return throwError ("%s.pwf - truncated waypoint file (count: %d, need: %d)", map, i, m_numWaypoints);
               }

               // more checks of waypoint quality
               if (m_paths[i]->pathNumber < 0 || m_paths[i]->pathNumber > m_numWaypoints) {
                  return throwError ("%s.pwf - bad waypoint file (path #%d index is out of bounds)", map, i);
               }
               addToBucket (m_paths[i]->origin, i);
            }
            m_waypointPaths = true;
         }
      }
      else {
         return throwError ("%s.pwf is not a yapb waypoint file (header found '%s' needed '%s'", map, header.header, FH_WAYPOINT);
      }
      fp.close ();
   }
   else {
      if (yb_waypoint_autodl_enable.boolean ()) {
         util.logEntry (true, LL_DEFAULT, "%s.pwf does not exist, trying to download from waypoint database", map);
         
         switch (downloadWaypoint ()) {
         case WDE_SOCKET_ERROR:
            return throwError ("%s.pwf does not exist. Can't autodownload. Socket error.", map);

         case WDE_CONNECT_ERROR:
            return throwError ("%s.pwf does not exist. Can't autodownload. Connection problems.", map);

         case WDE_NOTFOUND_ERROR:
            return throwError ("%s.pwf does not exist. Can't autodownload. Waypoint not available.", map);

         case WDE_NOERROR:
            util.logEntry (true, LL_DEFAULT, "%s.pwf was downloaded from waypoint database. Trying to load...", map);
            return load ();
         }
      }
      return throwError ("%s.pwf does not exist", map);
   }

   if (strncmp (header.author, "official", 7) == 0) {
      m_tempInfo.assign ("Using Official Waypoint File");
   }
   else {
      m_tempInfo.assign ("Using waypoint file by: %s", header.author);
   }
    
   for (int i = 0; i < m_numWaypoints; i++) {
      m_waypointDisplayTime[i] = 0.0f;
      m_waypointLightLevel[i] = 0.0f;
   }

   initPathMatrix ();
   initTypes ();

   m_waypointsChanged = false;
   m_pathDisplayTime = 0.0f;
   m_arrowDisplayTime = 0.0f;

   loadVisibility ();
   loadExperience ();

   extern ConVar yb_debug_goal;
   yb_debug_goal.set (INVALID_WAYPOINT_INDEX);

   return true;
}

void Waypoint::save (void) {
   WaypointHeader header;

   memset (header.mapName, 0, sizeof (header.mapName));
   memset (header.author, 0, sizeof (header.author));
   memset (header.header, 0, sizeof (header.header));

   strcpy (header.header, FH_WAYPOINT);
   strncpy (header.author, STRING (m_editor->v.netname), cr::bufsize (header.author));
   strncpy (header.mapName, game.getMapName (), cr::bufsize (header.mapName));

   header.mapName[31] = 0;
   header.fileVersion = FV_WAYPOINT;
   header.pointNumber = m_numWaypoints;

   File fp (getWaypointFilename (), "wb");

   // file was opened
   if (fp.isValid ()) {
      // write the waypoint header to the file...
      fp.write (&header, sizeof (header), 1);

      // save the waypoint paths...
      for (int i = 0; i < m_numWaypoints; i++) {
         fp.write (m_paths[i], sizeof (Path));
      }
      fp.close ();
   }
   else {
      util.logEntry (true, LL_ERROR, "Error writing '%s.pwf' waypoint file", game.getMapName ());
   }
}

const char *Waypoint::getWaypointFilename (bool isMemoryFile) {
   static String buffer;
   buffer.assign ("%s%s%s.pwf", getDataDirectory (isMemoryFile), util.isEmptyStr (yb_wptsubfolder.str ()) ? "" : yb_wptsubfolder.str (), game.getMapName ());

   if (File::exists (buffer)) {
      return buffer.chars ();
   }
   return util.format ("%s%s.pwf", getDataDirectory (isMemoryFile), game.getMapName ());
}

float Waypoint::calculateTravelTime (float maxSpeed, const Vector &src, const Vector &origin) {
   // this function returns 2D traveltime to a position

   return (origin - src).length2D () / maxSpeed;
}

bool Waypoint::isReachable (Bot *bot, int index) {
   // this function return whether bot able to reach index waypoint or not, depending on several factors.

   if (!bot || !exists (index)) {
      return false;
   }

   const Vector &src = bot->pev->origin;
   const Vector &dst = m_paths[index]->origin;

   // is the destination close enough?
   if ((dst - src).lengthSq () >= cr::square (320.0f)) {
      return false;
   }
   float ladderDist = (dst - src).length2D ();

   TraceResult tr;
   game.testLine (src, dst, TRACE_IGNORE_MONSTERS, bot->ent (), &tr);

   // if waypoint is visible from current position (even behind head)...
   if (tr.flFraction >= 1.0f) {

      // it's should be not a problem to reach waypoint inside water...
      if (bot->pev->waterlevel == 2 || bot->pev->waterlevel == 3) {
         return true;
      }

      // check for ladder
      bool nonLadder = !(m_paths[index]->flags & FLAG_LADDER) || ladderDist > 16.0f;

      // is dest waypoint higher than src? (62 is max jump height)
      if (nonLadder && dst.z > src.z + 62.0f) {
         return false; // can't reach this one
      }

      // is dest waypoint lower than src?
      if (nonLadder && dst.z < src.z - 100.0f) {
         return false; // can't reach this one
      }
      return true;
   }
   return false;
}

bool Waypoint::isNodeReacheable (const Vector &src, const Vector &destination) {
   TraceResult tr;

   float distance = (destination - src).length ();

   // is the destination not close enough?
   if (distance > m_autoPathDistance) {
      return false;
   }

   // check if we go through a func_illusionary, in which case return false
   game.testHull (src, destination, TRACE_IGNORE_MONSTERS, head_hull, m_editor, &tr);

   if (!game.isNullEntity (tr.pHit) && strcmp ("func_illusionary", STRING (tr.pHit->v.classname)) == 0) {
      return false; // don't add pathwaypoints through func_illusionaries
   }

   // check if this waypoint is "visible"...
   game.testLine (src, destination, TRACE_IGNORE_MONSTERS, m_editor, &tr);

   // if waypoint is visible from current position (even behind head)...
   if (tr.flFraction >= 1.0f || strncmp ("func_door", STRING (tr.pHit->v.classname), 9) == 0) {
      // if it's a door check if nothing blocks behind
      if (strncmp ("func_door", STRING (tr.pHit->v.classname), 9) == 0) {
         game.testLine (tr.vecEndPos, destination, TRACE_IGNORE_MONSTERS, tr.pHit, &tr);

         if (tr.flFraction < 1.0f) {
            return false;
         }
      }

      // check for special case of both waypoints being in water...
      if (engfuncs.pfnPointContents (src) == CONTENTS_WATER && engfuncs.pfnPointContents (destination) == CONTENTS_WATER) {
         return true; // then they're reachable each other
      }

      // is dest waypoint higher than src? (45 is max jump height)
      if (destination.z > src.z + 45.0f) {
         Vector sourceNew = destination;
         Vector destinationNew = destination;
         destinationNew.z = destinationNew.z - 50.0f; // straight down 50 units

         game.testLine (sourceNew, destinationNew, TRACE_IGNORE_MONSTERS, m_editor, &tr);

         // check if we didn't hit anything, if not then it's in mid-air
         if (tr.flFraction >= 1.0) {
            return false; // can't reach this one
         }
      }

      // check if distance to ground drops more than step height at points between source and destination...
      Vector direction = (destination - src).normalize (); // 1 unit long
      Vector check = src, down = src;

      down.z = down.z - 1000.0f; // straight down 1000 units

      game.testLine (check, down, TRACE_IGNORE_MONSTERS, m_editor, &tr);

      float lastHeight = tr.flFraction * 1000.0f; // height from ground
      distance = (destination - check).length (); // distance from goal

      while (distance > 10.0f) {
         // move 10 units closer to the goal...
         check = check + (direction * 10.0f);

         down = check;
         down.z = down.z - 1000.0f; // straight down 1000 units

         game.testLine (check, down, TRACE_IGNORE_MONSTERS, m_editor, &tr);

         float height = tr.flFraction * 1000.0f; // height from ground

         // is the current height greater than the step height?
         if (height < lastHeight - 18.0f) {
            return false; // can't get there without jumping...
         }
         lastHeight = height;
         distance = (destination - check).length (); // distance from goal
      }
      return true;
   }
   return false;
}

void Waypoint::rebuildVisibility (void) {
   if (!m_needsVisRebuild) {
      return;
   }

   TraceResult tr;
   uint8 res, shift;

   for (m_visibilityIndex = 0; m_visibilityIndex < m_numWaypoints; m_visibilityIndex++) {
      Vector sourceDuck = m_paths[m_visibilityIndex]->origin;
      Vector sourceStand = m_paths[m_visibilityIndex]->origin;

      if (m_paths[m_visibilityIndex]->flags & FLAG_CROUCH) {
         sourceDuck.z += 12.0f;
         sourceStand.z += 18.0f + 28.0f;
      }
      else {
         sourceDuck.z += -18.0f + 12.0f;
         sourceStand.z += 28.0f;
      }
      uint16 standCount = 0, crouchCount = 0;

      for (int i = 0; i < m_numWaypoints; i++) {
         // first check ducked visibility
         Vector dest = m_paths[i]->origin;

         game.testLine (sourceDuck, dest, TRACE_IGNORE_MONSTERS, nullptr, &tr);

         // check if line of sight to object is not blocked (i.e. visible)
         if (tr.flFraction != 1.0f || tr.fStartSolid) {
            res = 1;
         }
         else {
            res = 0;
         }
         res <<= 1;

         game.testLine (sourceStand, dest, TRACE_IGNORE_MONSTERS, nullptr, &tr);

         // check if line of sight to object is not blocked (i.e. visible)
         if (tr.flFraction != 1.0f || tr.fStartSolid) {
            res |= 1;
         }

         if (res != 0) {
            dest = m_paths[i]->origin;

            // first check ducked visibility
            if (m_paths[i]->flags & FLAG_CROUCH) {
               dest.z += 18.0f + 28.0f;
            }
            else {
               dest.z += 28.0f;
            }
            game.testLine (sourceDuck, dest, TRACE_IGNORE_MONSTERS, nullptr, &tr);

            // check if line of sight to object is not blocked (i.e. visible)
            if (tr.flFraction != 1.0f || tr.fStartSolid) {
               res |= 2;
            }
            else {
               res &= 1;
            }
            game.testLine (sourceStand, dest, TRACE_IGNORE_MONSTERS, nullptr, &tr);

            // check if line of sight to object is not blocked (i.e. visible)
            if (tr.flFraction != 1.0f || tr.fStartSolid) {
               res |= 1;
            }
            else {
               res &= 2;
            }
         }
         shift = (i % 4) << 1;

         m_visLUT[m_visibilityIndex][i >> 2] &= ~(3 << shift);
         m_visLUT[m_visibilityIndex][i >> 2] |= res << shift;

         if (!(res & 2)) {
            crouchCount++;
         }

         if (!(res & 1)) {
            standCount++;
         }
      }
      m_paths[m_visibilityIndex]->vis.crouch = crouchCount;
      m_paths[m_visibilityIndex]->vis.stand = standCount;
   }
   m_needsVisRebuild = false;
}

bool Waypoint::isVisible (int srcIndex, int destIndex) {
   if (!exists (srcIndex) || !exists (destIndex)) {
      return false;
   }

   uint8 res = m_visLUT[srcIndex][destIndex >> 2];
   res >>= (destIndex % 4) << 1;

   return !((res & 3) == 3);
}

bool Waypoint::isDuckVisible (int srcIndex, int destIndex) {
   if (!exists (srcIndex) || !exists (destIndex)) {
      return false;
   }

   uint8 res = m_visLUT[srcIndex][destIndex >> 2];
   res >>= (destIndex % 4) << 1;

   return !((res & 2) == 2);
}

bool Waypoint::isStandVisible (int srcIndex, int destIndex) {
   if (!exists (srcIndex) || !exists (destIndex)) {
      return false;
   }

   uint8 res = m_visLUT[srcIndex][destIndex >> 2];
   res >>= (destIndex % 4) << 1;

   return !((res & 1) == 1);
}

void Waypoint::frame (void) {
   // this function executes frame of waypoint operation code.

   if (game.isNullEntity (m_editor)) {
      return; // this function is only valid on listenserver, and in waypoint enabled mode.
   }

   // keep the clipping mode enabled, or it can be turned off after new round has started
   if (waypoints.hasEditFlag (WS_EDIT_NOCLIP) && util.isAlive (m_editor)) {
      m_editor->v.movetype = MOVETYPE_NOCLIP;
   }

   float nearestDistance = 99999.0f;
   int nearestIndex = INVALID_WAYPOINT_INDEX;

   // check if it's time to add jump waypoint
   if (m_learnJumpWaypoint) {
      if (!m_endJumpPoint) {
         if (m_editor->v.button & IN_JUMP) {
            push (9);

            m_timeJumpStarted = game.timebase ();
            m_endJumpPoint = true;
         }
         else {
            m_learnVelocity = m_editor->v.velocity;
            m_learnPosition = m_editor->v.origin;
         }
      }
      else if (((m_editor->v.flags & FL_ONGROUND) || m_editor->v.movetype == MOVETYPE_FLY) && m_timeJumpStarted + 0.1f < game.timebase () && m_endJumpPoint) {
         push (10);

         m_learnJumpWaypoint = false;
         m_endJumpPoint = false;
      }
   }

   // check if it's a autowaypoint mode enabled
   if (hasEditFlag (WS_EDIT_AUTO) && (m_editor->v.flags & (FL_ONGROUND | FL_PARTIALGROUND))) {
      // find the distance from the last used waypoint
      float distance = (m_lastWaypoint - m_editor->v.origin).lengthSq ();

      if (distance > 16384.0f) {
         // check that no other reachable waypoints are nearby...
         for (int i = 0; i < m_numWaypoints; i++) {
            if (isNodeReacheable (m_editor->v.origin, m_paths[i]->origin)) {
               distance = (m_paths[i]->origin - m_editor->v.origin).lengthSq ();

               if (distance < nearestDistance) {
                  nearestDistance = distance;
               }
            }
         }

         // make sure nearest waypoint is far enough away...
         if (nearestDistance >= 16384.0f) {
            push (0); // place a waypoint here
         }
      }
   }
   m_facingAtIndex = getFacingIndex ();

   // reset the minimal distance changed before
   nearestDistance = 999999.0f;

   // now iterate through all waypoints in a map, and draw required ones
   for (int i = 0; i < m_numWaypoints; i++) {
      float distance = (m_paths[i]->origin - m_editor->v.origin).length ();

      // check if waypoint is whitin a distance, and is visible
      if (distance < 512.0f && ((util.isVisible (m_paths[i]->origin, m_editor) && util.isInViewCone (m_paths[i]->origin, m_editor)) || !util.isAlive (m_editor) || distance < 128.0f)) {
         // check the distance
         if (distance < nearestDistance) {
            nearestIndex = i;
            nearestDistance = distance;
         }

         if (m_waypointDisplayTime[i] + 0.8f < game.timebase ()) {
            float nodeHeight = 0.0f;

            // check the node height
            if (m_paths[i]->flags & FLAG_CROUCH) {
               nodeHeight = 36.0f;
            }
            else {
               nodeHeight = 72.0f;
            }
            float nodeHalfHeight = nodeHeight * 0.5f;

            // all waypoints are by default are green
            Vector nodeColor;

            // colorize all other waypoints
            if (m_paths[i]->flags & FLAG_CAMP) {
               nodeColor = Vector (0, 255, 255);
            }
            else if (m_paths[i]->flags & FLAG_GOAL) {
               nodeColor = Vector (128, 0, 255);
            }
            else if (m_paths[i]->flags & FLAG_LADDER) {
               nodeColor = Vector (128, 64, 0);
            }
            else if (m_paths[i]->flags & FLAG_RESCUE) {
               nodeColor = Vector (255, 255, 255);
            }
            else {
               nodeColor = Vector (0, 255, 0);
            } 

            // colorize additional flags
            Vector nodeFlagColor = Vector (-1, -1, -1);

            // check the colors
            if (m_paths[i]->flags & FLAG_SNIPER) {
               nodeFlagColor = Vector (130, 87, 0);
            }
            else if (m_paths[i]->flags & FLAG_NOHOSTAGE) {
               nodeFlagColor = Vector (255, 255, 255);
            }
            else if (m_paths[i]->flags & FLAG_TF_ONLY) {
               nodeFlagColor = Vector (255, 0, 0);
            }
            else if (m_paths[i]->flags & FLAG_CF_ONLY) {
               nodeFlagColor = Vector (0, 0, 255);
            }
            int nodeWidth = 14;

            if (exists (m_facingAtIndex) && i == m_facingAtIndex) {
               nodeWidth *= 2;
            }

            // draw node without additional flags
            if (nodeFlagColor.x == -1) {
               game.drawLine (m_editor, m_paths[i]->origin - Vector (0, 0, nodeHalfHeight), m_paths[i]->origin + Vector (0, 0, nodeHalfHeight), nodeWidth + 1, 0, static_cast <int> (nodeColor.x), static_cast <int> (nodeColor.y), static_cast <int> (nodeColor.z), 250, 0, 10);
            }
            
            // draw node with flags
            else {
               game.drawLine (m_editor, m_paths[i]->origin - Vector (0, 0, nodeHalfHeight), m_paths[i]->origin - Vector (0, 0, nodeHalfHeight - nodeHeight * 0.75f), nodeWidth, 0, static_cast <int> (nodeColor.x), static_cast <int> (nodeColor.y), static_cast <int> (nodeColor.z), 250, 0, 10); // draw basic path
               game.drawLine (m_editor, m_paths[i]->origin - Vector (0, 0, nodeHalfHeight - nodeHeight * 0.75f), m_paths[i]->origin + Vector (0, 0, nodeHalfHeight), nodeWidth, 0, static_cast <int> (nodeFlagColor.x), static_cast <int> (nodeFlagColor.y), static_cast <int> (nodeFlagColor.z), 250, 0, 10); // draw additional path
            }
            m_waypointDisplayTime[i] = game.timebase ();
         }
      }
   }

   if (nearestIndex == INVALID_WAYPOINT_INDEX) {
      return;
   }

   // draw arrow to a some importaint waypoints
   if (exists (m_findWPIndex) || exists (m_cacheWaypointIndex) || exists (m_facingAtIndex)) {
      // check for drawing code
      if (m_arrowDisplayTime + 0.5f < game.timebase ()) {

         // finding waypoint - pink arrow
         if (m_findWPIndex != INVALID_WAYPOINT_INDEX) {
            game.drawLine (m_editor, m_editor->v.origin, m_paths[m_findWPIndex]->origin, 10, 0, 128, 0, 128, 200, 0, 5, DRAW_ARROW);
         }

         // cached waypoint - yellow arrow
         if (m_cacheWaypointIndex != INVALID_WAYPOINT_INDEX) {
            game.drawLine (m_editor, m_editor->v.origin, m_paths[m_cacheWaypointIndex]->origin, 10, 0, 255, 255, 0, 200, 0, 5, DRAW_ARROW);
         }

         // waypoint user facing at - white arrow
         if (m_facingAtIndex != INVALID_WAYPOINT_INDEX) {
            game.drawLine (m_editor, m_editor->v.origin, m_paths[m_facingAtIndex]->origin, 10, 0, 255, 255, 255, 200, 0, 5, DRAW_ARROW);
         }
         m_arrowDisplayTime = game.timebase ();
      }
   }

   // create path pointer for faster access
   Path *path = m_paths[nearestIndex];

   // draw a paths, camplines and danger directions for nearest waypoint
   if (nearestDistance <= 56.0f && m_pathDisplayTime <= game.timebase ()) {
      m_pathDisplayTime = game.timebase () + 1.0f;

      // draw the camplines
      if (path->flags & FLAG_CAMP) {
         Vector campSourceOrigin = path->origin + Vector (0.0f, 0.0f, 36.0f);

         // check if it's a source
         if (path->flags & FLAG_CROUCH) {
            campSourceOrigin = path->origin + Vector (0.0f, 0.0f, 18.0f);
         }
         Vector campStartOrigin = Vector (path->campStartX, path->campStartY, campSourceOrigin.z); // camp start
         Vector campEndOrigin = Vector (path->campEndX, path->campEndY, campSourceOrigin.z); // camp end

         // draw it now
         game.drawLine (m_editor, campSourceOrigin, campStartOrigin, 10, 0, 255, 0, 0, 200, 0, 10);
         game.drawLine (m_editor, campSourceOrigin, campEndOrigin, 10, 0, 255, 0, 0, 200, 0, 10);
      }

      // draw the connections
      for (int i = 0; i < MAX_PATH_INDEX; i++) {
         if (path->index[i] == INVALID_WAYPOINT_INDEX) {
            continue;
         }
         // jump connection
         if (path->connectionFlags[i] & PATHFLAG_JUMP) {
            game.drawLine (m_editor, path->origin, m_paths[path->index[i]]->origin, 5, 0, 255, 0, 128, 200, 0, 10);
         }
         else if (isConnected (path->index[i], nearestIndex)) { // twoway connection
            game.drawLine (m_editor, path->origin, m_paths[path->index[i]]->origin, 5, 0, 255, 255, 0, 200, 0, 10);
         }
         else { // oneway connection
            game.drawLine (m_editor, path->origin, m_paths[path->index[i]]->origin, 5, 0, 250, 250, 250, 200, 0, 10);
         }
      }

      // now look for oneway incoming connections
      for (int i = 0; i < m_numWaypoints; i++) {
         if (isConnected (m_paths[i]->pathNumber, path->pathNumber) && !isConnected (path->pathNumber, m_paths[i]->pathNumber)) {
            game.drawLine (m_editor, path->origin, m_paths[i]->origin, 5, 0, 0, 192, 96, 200, 0, 10);
         }
      }

      // draw the radius circle
      Vector origin = (path->flags & FLAG_CROUCH) ? path->origin : path->origin - Vector (0.0f, 0.0f, 18.0f);

      // if radius is nonzero, draw a full circle
      if (path->radius > 0.0f) {
         float sqr = cr::sqrtf (path->radius * path->radius * 0.5f);

         game.drawLine (m_editor, origin + Vector (path->radius, 0.0f, 0.0f), origin + Vector (sqr, -sqr, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);
         game.drawLine (m_editor, origin + Vector (sqr, -sqr, 0.0f), origin + Vector (0.0f, -path->radius, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);

         game.drawLine (m_editor, origin + Vector (0.0f, -path->radius, 0.0f), origin + Vector (-sqr, -sqr, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);
         game.drawLine (m_editor, origin + Vector (-sqr, -sqr, 0.0f), origin + Vector (-path->radius, 0.0f, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);

         game.drawLine (m_editor, origin + Vector (-path->radius, 0.0f, 0.0f), origin + Vector (-sqr, sqr, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);
         game.drawLine (m_editor, origin + Vector (-sqr, sqr, 0.0f), origin + Vector (0.0f, path->radius, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);

         game.drawLine (m_editor, origin + Vector (0.0f, path->radius, 0.0f), origin + Vector (sqr, sqr, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);
         game.drawLine (m_editor, origin + Vector (sqr, sqr, 0.0f), origin + Vector (path->radius, 0.0f, 0.0f), 5, 0, 0, 0, 255, 200, 0, 10);
      }
      else {
         float sqr = cr::sqrtf (32.0f);

         game.drawLine (m_editor, origin + Vector (sqr, -sqr, 0.0f), origin + Vector (-sqr, sqr, 0.0f), 5, 0, 255, 0, 0, 200, 0, 10);
         game.drawLine (m_editor, origin + Vector (-sqr, -sqr, 0.0f), origin + Vector (sqr, sqr, 0.0f), 5, 0, 255, 0, 0, 200, 0, 10);
      }

      // draw the danger directions
      if (!m_waypointsChanged) {
         int dangerIndex = getDangerIndex (game.getTeam (m_editor), nearestIndex, nearestIndex);

         if (exists (dangerIndex)) {
            game.drawLine (m_editor, path->origin, m_paths[dangerIndex]->origin, 15, 0, 255, 0, 0, 200, 0, 10, DRAW_ARROW); // draw a red arrow to this index's danger point
         }
      }

      auto getFlagsAsStr = [&] (int index) {
         Path *path = m_paths[index];

         // if this path is null, return
         if (path == nullptr) {
            return "\0";
         }
         bool jumpPoint = false;

         // iterate through connections and find, if it's a jump path
         for (int i = 0; i < MAX_PATH_INDEX; i++) {
            // check if we got a valid connection
            if (path->index[i] != INVALID_WAYPOINT_INDEX && (path->connectionFlags[i] & PATHFLAG_JUMP)) {
               jumpPoint = true;
            }
         }

         static String buffer;
         buffer.assign ("%s%s%s%s%s%s%s%s%s%s%s%s%s%s", (path->flags == 0 && !jumpPoint) ? " (none)" : "", (path->flags &FLAG_LIFT) ? " LIFT" : "", (path->flags &FLAG_CROUCH) ? " CROUCH" : "", (path->flags &FLAG_CROSSING) ? " CROSSING" : "", (path->flags &FLAG_CAMP) ? " CAMP" : "", (path->flags &FLAG_TF_ONLY) ? " TERRORIST" : "", (path->flags &FLAG_CF_ONLY) ? " CT" : "", (path->flags &FLAG_SNIPER) ? " SNIPER" : "", (path->flags &FLAG_GOAL) ? " GOAL" : "", (path->flags &FLAG_LADDER) ? " LADDER" : "", (path->flags &FLAG_RESCUE) ? " RESCUE" : "", (path->flags &FLAG_DOUBLEJUMP) ? " JUMPHELP" : "", (path->flags &FLAG_NOHOSTAGE) ? " NOHOSTAGE" : "", jumpPoint ? " JUMP" : "");

         // return the message buffer
         return buffer.chars ();
      };

      // display some information
      String waypointMessage;

      // show the information about that point
      waypointMessage.assign ("\n\n\n\n    Waypoint Information:\n\n"
                              "      Waypoint %d of %d, Radius: %.1f\n"
                              "      Flags: %s\n\n", nearestIndex, m_numWaypoints, path->radius, getFlagsAsStr (nearestIndex));

      // if waypoint is not changed display experience also
      if (!m_waypointsChanged) {
         int dangerIndexCT = getDangerIndex (TEAM_COUNTER, nearestIndex, nearestIndex);
         int dangerIndexT = getDangerIndex (TEAM_TERRORIST, nearestIndex, nearestIndex);

         waypointMessage.append ("      Experience Info:\n"
                                       "      CT: %d / %d dmg\n"
                                       "      T: %d / %d dmg\n", dangerIndexCT, dangerIndexCT != INVALID_WAYPOINT_INDEX ? getDangerDamage (TEAM_COUNTER, nearestIndex, dangerIndexCT) : 0, dangerIndexT, dangerIndexT != INVALID_WAYPOINT_INDEX ? getDangerDamage (TEAM_TERRORIST, nearestIndex, dangerIndexT) : 0);
      }

      // check if we need to show the cached point index
      if (m_cacheWaypointIndex != INVALID_WAYPOINT_INDEX) {
         waypointMessage.append ("\n    Cached Waypoint Information:\n\n"
                                       "      Waypoint %d of %d, Radius: %.1f\n"
                                       "      Flags: %s\n", m_cacheWaypointIndex, m_numWaypoints, m_paths[m_cacheWaypointIndex]->radius, getFlagsAsStr (m_cacheWaypointIndex));
      }

      // check if we need to show the facing point index
      if (m_facingAtIndex != INVALID_WAYPOINT_INDEX) {
         waypointMessage.append ("\n    Facing Waypoint Information:\n\n"
                                       "      Waypoint %d of %d, Radius: %.1f\n"
                                       "      Flags: %s\n", m_facingAtIndex, m_numWaypoints, m_paths[m_facingAtIndex]->radius, getFlagsAsStr (m_facingAtIndex));
      }

      // draw entire message
      MessageWriter (MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, Vector::null (), m_editor)
         .writeByte (TE_TEXTMESSAGE)
         .writeByte (4) // channel
         .writeShort (MessageWriter::fs16 (0, 1 << 13)) // x
         .writeShort (MessageWriter::fs16 (0, 1 << 13)) // y
         .writeByte (0) // effect
         .writeByte (255) // r1
         .writeByte (255) // g1
         .writeByte (255) // b1
         .writeByte (1) // a1
         .writeByte (255) // r2
         .writeByte (255) // g2
         .writeByte (255) // b2
         .writeByte (255) // a2
         .writeShort (0) // fadeintime
         .writeShort (0) // fadeouttime
         .writeShort (MessageWriter::fu16 (1.1f, 1 << 8)) // holdtime
         .writeString (waypointMessage.chars ());
   }
}

bool Waypoint::isConnected (int index) {
   for (int i = 0; i < m_numWaypoints; i++) {
      if (i == index) {
         continue;
      }
      for (auto &test : m_paths[i]->index) {
         if (test == index) {
            return true;
         }
      }
   }
   return false;
}

bool Waypoint::checkNodes (void) {
   int terrPoints = 0;
   int ctPoints = 0;
   int goalPoints = 0;
   int rescuePoints = 0;
   int i, j;

   for (i = 0; i < m_numWaypoints; i++) {
      int connections = 0;

      for (j = 0; j < MAX_PATH_INDEX; j++) {
         if (m_paths[i]->index[j] != INVALID_WAYPOINT_INDEX) {
            if (m_paths[i]->index[j] > m_numWaypoints) {
               util.logEntry (true, LL_WARNING, "Waypoint %d connected with invalid Waypoint #%d!", i, m_paths[i]->index[j]);
               return false;
            }
            connections++;
            break;
         }
      }

      if (connections == 0) {
         if (!isConnected (i)) {
            util.logEntry (true, LL_WARNING, "Waypoint %d isn't connected with any other Waypoint!", i);
            return false;
         }
      }

      if (m_paths[i]->pathNumber != i) {
         util.logEntry (true, LL_WARNING, "Waypoint %d pathnumber differs from index!", i);
         return false;
      }

      if (m_paths[i]->flags & FLAG_CAMP) {
         if (m_paths[i]->campEndX == 0.0f && m_paths[i]->campEndY == 0.0f) {
            util.logEntry (true, LL_WARNING, "Waypoint %d Camp-Endposition not set!", i);
            return false;
         }
      }
      else if (m_paths[i]->flags & FLAG_TF_ONLY) {
         terrPoints++;
      }
      else if (m_paths[i]->flags & FLAG_CF_ONLY) {
         ctPoints++;
      }
      else if (m_paths[i]->flags & FLAG_GOAL) {
         goalPoints++;
      }
      else if (m_paths[i]->flags & FLAG_RESCUE) {
         rescuePoints++;
      }

      for (int k = 0; k < MAX_PATH_INDEX; k++) {
         if (m_paths[i]->index[k] != INVALID_WAYPOINT_INDEX) {
            if (!exists (m_paths[i]->index[k])) {
               util.logEntry (true, LL_WARNING, "Waypoint %d - Pathindex %d out of Range!", i, k);
               engfuncs.pfnSetOrigin (m_editor, m_paths[i]->origin);

               setEditFlag (WS_EDIT_ENABLED | WS_EDIT_NOCLIP);
               return false;
            }
            else if (m_paths[i]->index[k] == i) {
               util.logEntry (true, LL_WARNING, "Waypoint %d - Pathindex %d points to itself!", i, k);

               engfuncs.pfnSetOrigin (m_editor, m_paths[i]->origin);
               setEditFlag (WS_EDIT_ENABLED | WS_EDIT_NOCLIP);

               return false;
            }
         }
      }
   }

   if (game.mapIs (MAP_CS)) {
      if (rescuePoints == 0) {
         util.logEntry (true, LL_WARNING, "You didn't set a Rescue Point!");
         return false;
      }
   }
   if (terrPoints == 0) {
      util.logEntry (true, LL_WARNING, "You didn't set any Terrorist Important Point!");
      return false;
   }
   else if (ctPoints == 0) {
      util.logEntry (true, LL_WARNING, "You didn't set any CT Important Point!");
      return false;
   }
   else if (goalPoints == 0) {
      util.logEntry (true, LL_WARNING, "You didn't set any Goal Point!");
      return false;
   }

   // perform DFS instead of floyd-warshall, this shit speedup this process in a bit
   PathWalk walk;
   Array <bool> visited;
   visited.reserve (m_numWaypoints);

   // first check incoming connectivity, initialize the "visited" table
   for (i = 0; i < m_numWaypoints; i++) {
      visited[i] = false;
   }
   walk.push (0); // always check from waypoint number 0

   while (!walk.empty ()) {
      // pop a node from the stack
      const int current = walk.first ();
      walk.shift ();

      visited[current] = true;

      for (j = 0; j < MAX_PATH_INDEX; j++) {
         int index = m_paths[current]->index[j];

         // skip this waypoint as it's already visited
         if (exists (index) && !visited[index]) {
            visited[index] = true;
            walk.push (index);
         }
      }
   }

   for (i = 0; i < m_numWaypoints; i++) {
      if (!visited[i]) {
         util.logEntry (true, LL_WARNING, "Path broken from Waypoint #0 to Waypoint #%d!", i);

         engfuncs.pfnSetOrigin (m_editor, m_paths[i]->origin);
         setEditFlag (WS_EDIT_ENABLED | WS_EDIT_NOCLIP);
         
         return false;
      }
   }

   // then check outgoing connectivity
   Array <IntArray> outgoingPaths; // store incoming paths for speedup
   outgoingPaths.reserve (m_numWaypoints);

   for (i = 0; i < m_numWaypoints; i++) {
      outgoingPaths[i].reserve (m_numWaypoints + 1);

      for (j = 0; j < MAX_PATH_INDEX; j++) {
         if (exists (m_paths[i]->index[j])) {
            outgoingPaths[m_paths[i]->index[j]].push (i);
         }
      }
   }

   // initialize the "visited" table
   for (i = 0; i < m_numWaypoints; i++) {
      visited[i] = false;
   }
   walk.clear ();
   walk.push (0); // always check from waypoint number 0

   while (!walk.empty ()) {
      const int current = walk.first (); // pop a node from the stack
      walk.shift ();

      for (auto &outgoing : outgoingPaths[current]) {
         if (visited[outgoing]) {
            continue; // skip this waypoint as it's already visited
         }
         visited[outgoing] = true;
         walk.push (outgoing);
      }
   }
   
   for (i = 0; i < m_numWaypoints; i++) {
      if (!visited[i]) {
         util.logEntry (true, LL_WARNING, "Path broken from Waypoint #%d to Waypoint #0!", i);

         engfuncs.pfnSetOrigin (m_editor, m_paths[i]->origin);
         setEditFlag (WS_EDIT_ENABLED | WS_EDIT_NOCLIP);

         return false;
      }
   }
   return true;
}

void Waypoint::initPathMatrix (void) {
   delete[] m_matrix;
   m_matrix = nullptr;

   m_matrix = new FloydMatrix[m_numWaypoints * m_numWaypoints + FastLZ::EXCESS];

   if (loadPathMatrix ()) {
      return; // matrix loaded from file
   }
   const int points = m_numWaypoints;

   for (int i = 0; i < points; i++) {
      for (int j = 0; j < points; j++) {
         (m_matrix + i * points + j)->dist = 999999;
         (m_matrix + i * points + j)->index = INVALID_WAYPOINT_INDEX;
      }
   }

   for (int i = 0; i < points; i++) {
      for (int j = 0; j < MAX_PATH_INDEX; j++) {
         if (!exists (m_paths[i]->index[j])) {
            continue;
         }
         (m_matrix + (i * points) + m_paths[i]->index[j])->dist = m_paths[i]->distances[j];
         (m_matrix + (i * points) + m_paths[i]->index[j])->index = m_paths[i]->index[j];
      }
   }

   for (int i = 0; i < points; i++) {
      (m_matrix + (i * points) + i)->dist = 0;
   }

   for (int k = 0; k < points; k++) {
      for (int i = 0; i < points; i++) {
         for (int j = 0; j < points; j++) {
            if ((m_matrix + (i * points) + k)->dist + (m_matrix + (k * points) + j)->dist < (m_matrix + (i * points) + j)->dist) {
               (m_matrix + (i * points) + j)->dist = (m_matrix + (i * points) + k)->dist + (m_matrix + (k * points) + j)->dist;
               (m_matrix + (i * points) + j)->index = (m_matrix + (i * points) + k)->index;
            }
         }
      }
   }

   // save path matrix to file for faster access
   savePathMatrix ();
}

int Waypoint::getPathDist (int srcIndex, int destIndex) {
   if (!exists (srcIndex) || !exists (destIndex)) {
      return 1;
   }
   return (m_matrix + (srcIndex * m_numWaypoints) + destIndex)->dist;
}

void Waypoint::setVisited (int index) {
   if (!exists (index)) {
      return;
   }
   if (!isVisited (index) && (m_paths[index]->flags & FLAG_GOAL)) {
      m_visitedGoals.push (index);
   }
}

void Waypoint::clearVisited (void) {
   m_visitedGoals.clear ();
}

bool Waypoint::isVisited (int index) {
   for (auto &visited : m_visitedGoals) {
      if (visited == index) {
         return true;
      }
   }
   return false;
}

void Waypoint::addBasic (void) {
   // this function creates basic waypoint types on map

   edict_t *ent = nullptr;

   // first of all, if map contains ladder points, create it
   while (!game.isNullEntity (ent = engfuncs.pfnFindEntityByString (ent, "classname", "func_ladder"))) {
      Vector ladderLeft = ent->v.absmin;
      Vector ladderRight = ent->v.absmax;
      ladderLeft.z = ladderRight.z;

      TraceResult tr;
      Vector up, down, front, back;

      Vector diff = ((ladderLeft - ladderRight) ^ Vector (0.0f, 0.0f, 0.0f)).normalize () * 15.0f;
      front = back = game.getAbsPos (ent);

      front = front + diff; // front
      back = back - diff; // back

      up = down = front;
      down.z = ent->v.absmax.z;

      game.testHull (down, up, TRACE_IGNORE_MONSTERS, point_hull, nullptr, &tr);

      if (engfuncs.pfnPointContents (up) == CONTENTS_SOLID || tr.flFraction != 1.0f) {
         up = down = back;
         down.z = ent->v.absmax.z;
      }

      game.testHull (down, up - Vector (0.0f, 0.0f, 1000.0f), TRACE_IGNORE_MONSTERS, point_hull, nullptr, &tr);
      up = tr.vecEndPos;

      Vector point = up + Vector (0.0f, 0.0f, 39.0f);
      m_isOnLadder = true;

      do {
         if (getNearestNoBuckets (point, 50.0f) == INVALID_WAYPOINT_INDEX) {
            push (3, point);
         }
         point.z += 160;
      } while (point.z < down.z - 40.0f);

      point = down + Vector (0.0f, 0.0f, 38.0f);

      if (getNearestNoBuckets (point, 50.0f) == INVALID_WAYPOINT_INDEX) {
         push (3, point);
      }
      m_isOnLadder = false;
   }

   auto autoCreateForEntity = [](int type, const char *entity) {
      edict_t *ent = nullptr;

      while (!game.isNullEntity (ent = engfuncs.pfnFindEntityByString (ent, "classname", entity))) {
         const Vector &pos = game.getAbsPos (ent);

         if (waypoints.getNearestNoBuckets (pos, 50.0f) == INVALID_WAYPOINT_INDEX) {
            waypoints.push (type, pos);
         }
      }
   };

   autoCreateForEntity (0, "info_player_deathmatch"); // then terrortist spawnpoints
   autoCreateForEntity (0, "info_player_start"); // then add ct spawnpoints
   autoCreateForEntity (0, "info_vip_start"); // then vip spawnpoint
   autoCreateForEntity (0, "armoury_entity"); // weapons on the map ?

   autoCreateForEntity (4, "func_hostage_rescue"); // hostage rescue zone
   autoCreateForEntity (4, "info_hostage_rescue"); // hostage rescue zone (same as above)

   autoCreateForEntity (100, "func_bomb_target"); // bombspot zone
   autoCreateForEntity (100, "info_bomb_target"); // bombspot zone (same as above)
   autoCreateForEntity (100, "hostage_entity"); // hostage entities
   autoCreateForEntity (100, "func_vip_safetyzone"); // vip rescue (safety) zone
   autoCreateForEntity (100, "func_escapezone"); // terrorist escape zone
}

void Waypoint::eraseFromDisk (void) {
   // this function removes waypoint file from the hard disk

   StringArray forErase;
   const char *map = game.getMapName ();

   bots.kickEveryone (true);

   // if we're delete waypoint, delete all corresponding to it files
   forErase.push (util.format ("%s%s.pwf", getDataDirectory (), map)); // waypoint itself
   forErase.push (util.format ("%slearned/%s.exp", getDataDirectory (), map)); // corresponding to waypoint experience
   forErase.push (util.format ("%slearned/%s.vis", getDataDirectory (), map)); // corresponding to waypoint vistable
   forErase.push (util.format ("%slearned/%s.pmt", getDataDirectory (), map)); // corresponding to waypoint path matrix

   for (auto &item : forErase) {
      if (File::exists (const_cast <char *> (item.chars ()))) {
         _unlink (item.chars ());
         util.logEntry (true, LL_DEFAULT, "File %s, has been deleted from the hard disk", item.chars ());
      }
      else {
         util.logEntry (true, LL_ERROR, "Unable to open %s", item.chars ());
      }
   }
   init (); // reintialize points
}

const char *Waypoint::getDataDirectory (bool isMemoryFile) {
   static String buffer;
   buffer.clear ();

   if (isMemoryFile) {
      buffer.assign ("addons/yapb/data/");
   }
   else {
      buffer.assign ("%s/addons/yapb/data/", game.getModName ());
   }
   return buffer.chars ();
}

void Waypoint::setBombPos (bool reset, const Vector &pos) {
   // this function stores the bomb position as a vector

   if (reset) {
      m_bombPos.nullify ();
      bots.setBombPlanted (false);

      return;
   }

   if (!pos.empty ()) {
      m_bombPos = pos;
      return;
   }
   edict_t *ent = nullptr;

   while (!game.isNullEntity (ent = engfuncs.pfnFindEntityByString (ent, "classname", "grenade"))) {
      if (strcmp (STRING (ent->v.model) + 9, "c4.mdl") == 0) {
         m_bombPos = game.getAbsPos (ent);
         break;
      }
   }
}

void Waypoint::startLearnJump (void) {
   m_learnJumpWaypoint = true;
}

void Waypoint::setSearchIndex (int index) {
   m_findWPIndex = index;

   if (exists (m_findWPIndex)) {
      ctrl.msg ("Showing Direction to Waypoint #%d", m_findWPIndex);
   }
   else {
      m_findWPIndex = INVALID_WAYPOINT_INDEX;
   }
}

Waypoint::Waypoint (void) {
   cleanupPathMemory ();
   
   memset (m_visLUT, 0, sizeof (m_visLUT));
   memset (m_waypointDisplayTime, 0, sizeof (m_waypointDisplayTime));
   memset (m_waypointLightLevel, 0, sizeof (m_waypointLightLevel));

   m_waypointPaths = false;
   m_endJumpPoint = false;
   m_needsVisRebuild = false;
   m_learnJumpWaypoint = false;
   m_waypointsChanged = false;
   m_timeJumpStarted = 0.0f;

   m_lastJumpWaypoint = INVALID_WAYPOINT_INDEX;
   m_cacheWaypointIndex = INVALID_WAYPOINT_INDEX;
   m_findWPIndex = INVALID_WAYPOINT_INDEX;
   m_facingAtIndex = INVALID_WAYPOINT_INDEX;
   m_visibilityIndex = 0;
   m_loadTries = 0;
   m_numWaypoints = 0;
   m_isOnLadder = false;

   m_terrorPoints.clear ();
   m_ctPoints.clear ();
   m_goalPoints.clear ();
   m_campPoints.clear ();
   m_rescuePoints.clear ();
   m_sniperPoints.clear ();

   m_matrix = nullptr;
   m_editor = nullptr;

   for (auto &path : m_paths) {
      path = nullptr;
   }
}

Waypoint::~Waypoint (void) {

   // free floyd warshall
   delete[] m_matrix;
   m_matrix = nullptr;

   // free experience stuff
   delete[] m_experience;
   m_experience = nullptr;

   cleanupPathMemory ();
}

void Waypoint::closeSocket (int sock) {
#if defined(PLATFORM_WIN32)
   if (sock != -1) {
      closesocket (sock);
   }
   WSACleanup ();
#else
   if (sock != -1)
      close (sock);
#endif
}

WaypointDownloadError Waypoint::downloadWaypoint (void) {
#if defined(PLATFORM_WIN32)
   WORD requestedVersion = MAKEWORD (1, 1);
   WSADATA wsaData;

   int wsa = WSAStartup (requestedVersion, &wsaData);

   if (wsa != 0) {
      return WDE_SOCKET_ERROR;
   }
#endif

   hostent *host = gethostbyname (yb_waypoint_autodl_host.str ());

   if (host == nullptr) {
      return WDE_SOCKET_ERROR;
   }
   auto socketHandle = static_cast <int> (socket (AF_INET, SOCK_STREAM, 0));

   if (socketHandle < 0) {
      closeSocket (socketHandle);
      return WDE_SOCKET_ERROR;
   }
   sockaddr_in dest;

   timeval timeout;
   timeout.tv_sec = 5;
   timeout.tv_usec = 0;

   int result = setsockopt (socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast <char *> (&timeout), sizeof (timeout));

   if (result < 0) {
      closeSocket (socketHandle);
      return WDE_SOCKET_ERROR;
   }
   result = setsockopt (socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast <char *> (&timeout), sizeof (timeout));

   if (result < 0) {
      closeSocket (socketHandle);
      return WDE_SOCKET_ERROR;
   }
   memset (&dest, 0, sizeof (dest));

   dest.sin_family = AF_INET;
   dest.sin_port = htons (80);
   dest.sin_addr.s_addr = inet_addr (inet_ntoa (*(reinterpret_cast <in_addr *> (host->h_addr))));

   if (connect (socketHandle, reinterpret_cast <sockaddr *> (&dest), static_cast <int> (sizeof (dest))) == -1) {
      closeSocket (socketHandle);
      return WDE_CONNECT_ERROR;
   }

   String request;
   request.assign ("GET /wpdb/%s.pwf HTTP/1.0\r\nAccept: */*\r\nUser-Agent: %s/%s\r\nHost: %s\r\n\r\n", game.getMapName (), PRODUCT_SHORT_NAME, PRODUCT_VERSION, yb_waypoint_autodl_host.str ());

   if (send (socketHandle, request.chars (), static_cast <int> (request.length () + 1), 0) < 1) {
      closeSocket (socketHandle);
      return WDE_SOCKET_ERROR;
   }

   const int ChunkSize = MAX_PRINT_BUFFER;
   char buffer[ChunkSize] = { 0, };

   bool finished = false;
   int recvPosition = 0;
   int symbolsInLine = 0;

   // scan for the end of the header
   while (!finished && recvPosition < ChunkSize) { 
      if (recv (socketHandle, &buffer[recvPosition], 1, 0) == 0) {
         finished = true;
      }

      // ugly, but whatever
      if (recvPosition > 2 && buffer[recvPosition - 2] == '4' && buffer[recvPosition - 1] == '0' && buffer[recvPosition] == '4') {
         closeSocket (socketHandle);
         return WDE_NOTFOUND_ERROR;
      }

      switch (buffer[recvPosition]) {
      case '\r':
         break;

      case '\n':
         if (symbolsInLine == 0) {
            finished = true;
         }
         symbolsInLine = 0;
         break;

      default:
         symbolsInLine++;
         break;
      }
      recvPosition++;
   }

   File fp (waypoints.getWaypointFilename (), "wb");

   if (!fp.isValid ()) {
      closeSocket (socketHandle);
      return WDE_SOCKET_ERROR;
   }
   int recvSize = 0;

   do {
      recvSize = recv (socketHandle, buffer, ChunkSize, 0);

      if (recvSize > 0) {
         fp.write (buffer, recvSize);
         fp.flush ();
      }

   } while (recvSize != 0);

   fp.close ();
   closeSocket (socketHandle);

   return WDE_NOERROR;
}

void Waypoint::initBuckets (void) {
   m_numWaypoints = 0;

   for (int x = 0; x < MAX_WAYPOINT_BUCKET_MAX; x++) {
      for (int y = 0; y < MAX_WAYPOINT_BUCKET_MAX; y++) {
         for (int z = 0; z < MAX_WAYPOINT_BUCKET_MAX; z++) {
            m_buckets[x][y][z].reserve (MAX_WAYPOINT_BUCKET_WPTS);
            m_buckets[x][y][z].clear ();
         }
      }
   }
}

void Waypoint::addToBucket (const Vector &pos, int index) {
   const Bucket &bucket = locateBucket (pos);
   m_buckets[bucket.x][bucket.y][bucket.z].push (index);
}

void Waypoint::eraseFromBucket (const Vector &pos, int index) {
   const Bucket &bucket = locateBucket (pos);
   IntArray &data = m_buckets[bucket.x][bucket.y][bucket.z];

   for (size_t i = 0; i < data.length (); i++) {
      if (data[i] == index) {
         data.erase (i, 1);
         break;
      }
   }
}

Waypoint::Bucket Waypoint::locateBucket (const Vector &pos) {
   constexpr float size = 4096.0f;

   return {
       cr::abs (static_cast <int> ((pos.x + size) / MAX_WAYPOINT_BUCKET_SIZE)),
       cr::abs (static_cast <int> ((pos.y + size) / MAX_WAYPOINT_BUCKET_SIZE)),
       cr::abs (static_cast <int> ((pos.z + size) / MAX_WAYPOINT_BUCKET_SIZE))
   };
}

IntArray &Waypoint::getWaypointsInBucket (const Vector &pos) {
   const Bucket &bucket = locateBucket (pos);
   return m_buckets[bucket.x][bucket.y][bucket.z];
}

void Waypoint::updateGlobalExperience (void) {
   // this function called after each end of the round to update knowledge about most dangerous waypoints for each team.

   // no waypoints, no experience used or waypoints edited or being edited?
   if (m_numWaypoints < 1 || m_waypointsChanged) {
      return; // no action
   }
   bool adjustValues = false;

   // get the most dangerous waypoint for this position for both teams
   for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
      int bestIndex = INVALID_WAYPOINT_INDEX; // best index to store
      int maxDamage = 0;

      for (int i = 0; i < waypoints.length (); i++) {
         maxDamage = 0;
         bestIndex = INVALID_WAYPOINT_INDEX;

         for (int j = 0; j < waypoints.length (); j++) {
            if (i == j) {
               continue;
            }
            int actDamage = getDangerDamage (team, i, j);

            if (actDamage > maxDamage) {
               maxDamage = actDamage;
               bestIndex = j;
            }
         }

         if (maxDamage > MAX_DAMAGE_VALUE) {
            adjustValues = true;
         }
         (m_experience + (i * m_numWaypoints) + i)->index[team] = bestIndex;
      }
   }
   constexpr int HALF_DAMAGE_VALUE = static_cast <int> (MAX_DAMAGE_VALUE * 0.5);

   // adjust values if overflow is about to happen
   if (adjustValues) {
      for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
         for (int i = 0; i < m_numWaypoints; i++) {
            for (int j = 0; j < m_numWaypoints; j++) {
               if (i == j) {
                  continue;
               }
               (m_experience + (i * m_numWaypoints) + j)->damage[team] = cr::clamp (getDangerDamage (team, i, j) - HALF_DAMAGE_VALUE, 0, MAX_DAMAGE_VALUE);
            }
         }
      }
   }

   for (int team = TEAM_TERRORIST; team < MAX_TEAM_COUNT; team++) {
      m_highestDamage[team] = cr::clamp (m_highestDamage [team] - HALF_DAMAGE_VALUE, 1, MAX_DAMAGE_VALUE);
   }
}

int Waypoint::getDangerIndex (int team, int start, int goal) {
   if (team != TEAM_TERRORIST && team != TEAM_COUNTER) {
      return INVALID_WAYPOINT_INDEX;
   }

   // realiablity check
   if (!exists (start) || !exists (goal)) {
      return INVALID_WAYPOINT_INDEX;
   }
   return (m_experience + (start * m_numWaypoints) + goal)->index[team];
}

int Waypoint::getDangerValue (int team, int start, int goal) {
   if (team != TEAM_TERRORIST && team != TEAM_COUNTER) {
      return 0;
   }

   // reliability check
   if (!exists (start) || !exists (goal)) {
      return 0;
   }
   return (m_experience + (start * m_numWaypoints) + goal)->value[team];
}

int Waypoint::getDangerDamage (int team, int start, int goal) {
   if (team != TEAM_TERRORIST && team != TEAM_COUNTER) {
      return 0;
   }

   // reliability check
   if (!exists (start) || !exists (goal)) {
      return 0;
   }
   return (m_experience + (start * m_numWaypoints) + goal)->damage[team];
}