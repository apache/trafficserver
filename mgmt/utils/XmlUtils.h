/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#ifndef _INKXML_H_
#define _INKXML_H_
#include <stdio.h>

/****************************************************************************
 *
 *  XmlUtils.h - Functions for interfacing to XML parser (expat)
 *
 *
 *
 *
 *   This code was taken from the xmlparse library developed
 *   by Xing Xiong for SynText.
 ****************************************************************************/

/**************************************************
 XMLNode definition.
 **************************************************/

typedef struct
{
  char *pAName;                 /*Element Name. */
  char *pAValue;                /*Element Value. */
} Attribute;

class XMLNode
{
public:
  XMLNode();
  ~XMLNode();

  int getChildCount()
  {
    return m_nChildCount;
  }

  /* Calculate how many children has the input TagName. */
  int getChildCount(const char *pTagName);

  /* Index a childNode in the child List. */
  XMLNode *getChildNode(int nIndex = 0);

  /* Index the "nIndex"th child with given tag Name. */
  XMLNode *getChildNode(const char *pTagName, int nIndex = 0);
  XMLNode *getParentNode()
  {
    return m_pParentNode;
  }

  /* Input is: "TagName1/TagName2/TagName3" */
  XMLNode *getNodeByPath(const char *pPath);

  XMLNode *getNext()
  {
    return m_pNext;
  }
  void AppendChild(XMLNode * p);

  char *getNodeName()
  {
    return m_pNodeName;
  }
  char *getNodeValue()
  {
    return m_pNodeValue;
  }
  char *getNodeValue(const char *pPath);
  char *getAttributeValueByName(const char *pAName);

  /* will return full XML text of this node(including the children)
     return NULL if error.
     Attention: user need to free the returned string, using "delete".
   */
  char *getXML();

  int setNodeName(const char *psName);
  int setNodeValue(const char *psValue);

  /* the ppsAttr should contains 2N+1 char*.
     The last one of pssAttr should be a NULL.
   */
  int setAttributes(char **ppsAttr);

  void WriteFile(FILE * pf);

  /* Testing method */
  void PrintAll();

/*need a better way to solve this. */
/*protected: */
  char *m_pNodeName;
  char *m_pNodeValue;

  int m_nChildCount;
  XMLNode *m_pFirstChild;       /* the first Child. */
  XMLNode *m_pLastChild;        /* the last  Child.     */
  XMLNode *m_pParentNode;
  XMLNode *m_pNext;

  int m_nACount;                /* Attribute number. */
  Attribute *m_pAList;          /* Attribute List.   */
private:
  char *getAttributeString();
};

/**************************************************
 XMLDom definition.
 **************************************************/

class XMLDom:public XMLNode
{
public:
  XMLDom();
  ~XMLDom();

  int LoadXML(const char *pXml);
  int LoadFile(const char *psFileName);
  int SaveToFile(const char *psFileName);

/*
protected:
	void elementStart(const char* el, const char** attr);
	void elementEnd(const char* el);
	void charHandler(const char *s,int len);
*/

  XMLNode *m_pCur;              /* the current XMLNode. used during parsing. */
};

#endif
