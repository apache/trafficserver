--
-- Licensed to the Apache Software Foundation (ASF) under one
-- or more contributor license agreements.  See the NOTICE file
-- distributed with this work for additional information
-- regarding copyright ownership.  The ASF licenses this file
-- to you under the Apache License, Version 2.0 (the
-- "License"); you may not use this file except in compliance
-- with the License.  You may obtain a copy of the License at
--
--      http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.
--

-- MySQL dump 10.13  Distrib 5.1.48, for apple-darwin10.4.0 (i386)
--
-- Host: localhost    Database: mysql_remap
-- ------------------------------------------------------
-- Server version	5.1.48-log

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `hostname`
--

DROP TABLE IF EXISTS `hostname`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `hostname` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `hostname` varchar(255) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `hostname` (`hostname`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `hostname`
--

LOCK TABLES `hostname` WRITE;
/*!40000 ALTER TABLE `hostname` DISABLE KEYS */;
INSERT INTO `hostname` VALUES (1,'www.ericbalsa.com'),(2,'www.google.com');
/*!40000 ALTER TABLE `hostname` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `map`
--

DROP TABLE IF EXISTS `map`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `map` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `from_scheme_id` int(10) unsigned NOT NULL DEFAULT '1',
  `from_hostname_id` int(10) unsigned NOT NULL,
  `from_port` int(5) unsigned NOT NULL,
  `to_scheme_id` int(10) unsigned NOT NULL DEFAULT '1',
  `to_hostname_id` int(10) unsigned NOT NULL,
  `to_port` int(5) unsigned NOT NULL DEFAULT '80',
  `is_enabled` tinyint(1) unsigned NOT NULL DEFAULT '1',
  PRIMARY KEY (`id`),
  UNIQUE KEY `from_unique` (`from_scheme_id`,`from_hostname_id`,`from_port`),
  UNIQUE KEY `to_unique` (`to_scheme_id`,`to_hostname_id`,`to_port`),
  UNIQUE KEY `unique_across_everything` (`from_scheme_id`,`from_hostname_id`,`from_port`,`to_scheme_id`,`to_hostname_id`,`to_port`),
  KEY `to_hostname_id` (`to_hostname_id`),
  KEY `from_hostname_id` (`from_hostname_id`),
  CONSTRAINT `map_ibfk_1` FOREIGN KEY (`from_scheme_id`) REFERENCES `scheme` (`id`) ON UPDATE CASCADE,
  CONSTRAINT `map_ibfk_3` FOREIGN KEY (`to_scheme_id`) REFERENCES `scheme` (`id`) ON UPDATE CASCADE,
  CONSTRAINT `map_ibfk_4` FOREIGN KEY (`to_hostname_id`) REFERENCES `hostname` (`id`) ON UPDATE CASCADE,
  CONSTRAINT `map_ibfk_5` FOREIGN KEY (`from_hostname_id`) REFERENCES `hostname` (`id`) ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `map`
--

LOCK TABLES `map` WRITE;
/*!40000 ALTER TABLE `map` DISABLE KEYS */;
INSERT INTO `map` VALUES (1,1,2,80,1,1,80,1);
/*!40000 ALTER TABLE `map` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `scheme`
--

DROP TABLE IF EXISTS `scheme`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `scheme` (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `scheme_desc` varchar(5) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `desc` (`scheme_desc`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `scheme`
--

LOCK TABLES `scheme` WRITE;
/*!40000 ALTER TABLE `scheme` DISABLE KEYS */;
INSERT INTO `scheme` VALUES (1,'http'),(2,'https');
/*!40000 ALTER TABLE `scheme` ENABLE KEYS */;
UNLOCK TABLES;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2010-11-15 21:08:43
