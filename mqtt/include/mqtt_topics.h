/*******************************************************************************
 * Copyright (c) 2007, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/


#if !defined(MQTT_TOPICS_H)
#define MQTT_TOPICS_H

#define TOPIC_LEVEL_SEPARATOR "/"
#define SINGLE_LEVEL_WILDCARD "+"
#define MULTI_LEVEL_WILDCARD "#"

int Topics_isValidName(char* aName);

int Topics_hasWildcards(char* topic);

int Topics_matches(char* wildTopic, int wildcards, char* topic);

#endif /* MQTT_TOPICS_H */

