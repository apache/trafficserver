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

#include "expat.h"
#include <stdio.h>
#include <stdlib.h>
#include "ink_platform.h"
#include <sys/types.h>
#include <sys/stat.h>

#include "XmlUtils.h"
#include "ink_error.h"

/****************************************************************************
 *
 *  XmlUtils.cc - Functions for interfacing to XML parser (expat)
 *
 *
 *
 *
 *   This code was taken from the xmlparse library developed
 *   by Xing Xiong for SynText.
 ****************************************************************************/

const char *gcstr_lb = "<";
const char *gcstr_rb = ">";
const char *gcstr_lbend = "</";
const char *gcstr_rbend = "/>";

void
XMLNode::PrintAll()
{
  XMLNode *p;
  p = m_pFirstChild;
  while (p) {
    p->PrintAll();
    p = p->m_pNext;
  }
}

int
alloc_and_copy(char **pDest, const char *pSource)
{
  int len;
  if (NULL == pSource)
    return 1;

  len = strlen(pSource);
  *pDest = new char[len + 1];
  if (NULL == *pDest)
    return 2;

  memcpy(*pDest, pSource, len);
  (*pDest)[len] = 0;
  return 0;
}

int
XMLNode::setNodeName(const char *psName)
{
  return alloc_and_copy(&m_pNodeName, psName);
}

int
XMLNode::setNodeValue(const char *psValue)
{
  return alloc_and_copy(&m_pNodeValue, psValue);
}


/* the ppsAttr should contains 2N+1 char*.
   The last one of pssAttr should be a NULL.
 */
int
XMLNode::setAttributes(char **ppsAttr)
{
  int i;
  if (m_pAList) {
    for (i = 0; i < m_nACount; i++) {
      delete m_pAList[i].pAName;
      delete m_pAList[i].pAValue;
    }

    delete[]m_pAList;
    m_pAList = NULL;
    m_nACount = 0;
  }

  int nCount = 0;
  for (i = 0; ppsAttr[i]; i += 2)
    nCount++;                   /*Calculate how many elements. */
  if (nCount == 0)
    return 0;

  m_nACount = nCount;
  m_pAList = new Attribute[nCount];
  for (i = 0; i < nCount; i++) {
    /* Attribute Name: attr[2*i], Value: attr[2*i+1]; */
    alloc_and_copy(&m_pAList[i].pAName, ppsAttr[2 * i]);
    alloc_and_copy(&m_pAList[i].pAValue, ppsAttr[2 * i + 1]);
  }
  return 0;
}


XMLNode::XMLNode()
{
  m_pNodeName = NULL;
  m_pNodeValue = NULL;
  m_pNext = NULL;
  m_pParentNode = NULL;
  m_pFirstChild = NULL;
  m_nChildCount = 0;

  m_nACount = 0;
  m_pAList = NULL;
  m_pLastChild = 0;
}

XMLNode::~XMLNode()
{
  XMLNode *p;

  /* delete NodeName and NodeValue. */
  if (m_pNodeName) {
    delete m_pNodeName;
    m_pNodeName = NULL;
  }
  if (m_pNodeValue) {
    delete m_pNodeValue;
    m_pNodeValue = NULL;
  }

  /* delete all attributes. */
  if (m_nACount) {
    for (int i = 0; i < m_nACount; i++) {
      delete m_pAList[i].pAName;
      delete m_pAList[i].pAValue;
    }
    delete m_pAList;
    m_pAList = NULL;
  }

  /* delete all the children. */
  p = m_pFirstChild;
  while (p) {
    XMLNode *pTemp = p->m_pNext;
    delete p;
    p = pTemp;
  }
  m_pFirstChild = NULL;
}

int
XMLNode::getChildCount(const char *pTagName)
{
  XMLNode *p;
  int nCount = 0;
  if (m_nChildCount == 0)
    return 0;

  p = m_pFirstChild;
  while (p) {
    if (!strcmp(p->m_pNodeName, pTagName))
      nCount++;
    p = p->m_pNext;
  }

  return nCount;
}

XMLNode *
XMLNode::getChildNode(int nIndex)
{
  XMLNode *p = m_pFirstChild;
  while (nIndex--) {
    if (p == NULL)
      return NULL;
    p = p->m_pNext;
  }
  return p;
}

XMLNode *
XMLNode::getChildNode(const char *pTagName, int nIndex)
{
  XMLNode *p = m_pFirstChild;
  while (nIndex >= 0) {
    /* try to find the next child node with given name. */
    while (p && strcmp(p->m_pNodeName, pTagName))
      p = p->m_pNext;
    if (p == NULL)
      return NULL;
    nIndex--;
    if (nIndex >= 0)
      p = p->m_pNext;
  }
  return p;
}

void
XMLNode::AppendChild(XMLNode * p)
{
  if (m_nChildCount == 0) {
    m_pFirstChild = p;
    m_pLastChild = p;
  } else {
    m_pLastChild->m_pNext = p;
    m_pLastChild = p;
  }

  //p->m_pNext = NULL;
  p->m_pParentNode = this;
  m_nChildCount++;
}

/* <pName pAttr>pValue</pName>
   pValue may be NULL.
   pAttr  may be NULL, otherwise, there is already a " " at its beginning.
 */
char *
ConstructXMLBlock(const char *pName, const char *pValue, const char *pAttr)
{
  int NameLen = strlen(pName);
  int ValueLen = 0;
  if (pValue)
    ValueLen = strlen(pValue);
  int AttrLen = 0;
  if (pAttr)
    AttrLen = strlen(pAttr);

  char *p = new char[NameLen * 2 + AttrLen + ValueLen + 6];
  p[0] = '<';

  int nOffset = 1;
  memcpy(p + nOffset, pName, NameLen);
  nOffset += NameLen;

  if (pAttr) {
    memcpy(p + nOffset, pAttr, AttrLen);
    nOffset += AttrLen;
  }

  p[nOffset++] = '>';
  if (pValue) {
    memcpy(p + nOffset, pValue, ValueLen);
    nOffset += ValueLen;
  }

  p[nOffset++] = '<';
  p[nOffset++] = '/';
  memcpy(p + nOffset, pName, NameLen);
  nOffset += NameLen;

  p[nOffset++] = '>';
  p[nOffset] = 0;
  /*p[NameLen*2+5+ValueLen] = 0; */
  return p;
}

/* Allocate a new piece of memory to contain the new string.
   p1 may be NULL.
   if p1 is not NULL, then delete it.
 */
char *
AppendStr(char *p1, const char *p2)
{
  int nLen1 = 0;
  if (p1)
    nLen1 = strlen(p1);

  int nLen2 = strlen(p2);
  char *p = new char[nLen1 + nLen2 + 1];
  if (!p)
    return NULL;

  if (p1) {
    memcpy(p, p1, nLen1);
    delete p1;
  }
  memcpy(p + nLen1, p2, nLen2);
  p[nLen1 + nLen2] = 0;
  return p;
}

char *
XMLNode::getAttributeString()
{
  char *pAttr = NULL;
  for (int i = 0; i < m_nACount; i++) {
    // coverity[new_array][var_assign][delete_var]
    pAttr = AppendStr(pAttr, " ");
    // coverity[new_array][var_assign][delete_var]
    pAttr = AppendStr(pAttr, m_pAList[i].pAName);

    // coverity [delete_var]
    pAttr = AppendStr(pAttr, "=\"");

    // coverity[new_array][var_assign]
    pAttr = AppendStr(pAttr, m_pAList[i].pAValue);
    // coverity[new_array][var_assign][delete_var]
    pAttr = AppendStr(pAttr, "\"");
  }

  return pAttr;
}

/*
   There are better ways to implement this function.
 */
char *
XMLNode::getXML()
{
  char *pBody = NULL;
  if (m_pNodeValue)
    // coverity[var_assign][new_array]
    pBody = AppendStr(NULL, m_pNodeValue);

  XMLNode *pChild = m_pFirstChild;
  while (pChild) {
    char *pChildXML = pChild->getXML();
    if (!pChildXML) {
      if (pBody)
        delete[]pBody;
      return NULL;
    }

    pBody = AppendStr(pBody, pChildXML);
    delete pChildXML;

    pChild = pChild->m_pNext;
  }

  char *pAttr = getAttributeString();
  char *pRet = ConstructXMLBlock(m_pNodeName, pBody, pAttr);
  if (pBody)
    delete[]pBody;
  if (pAttr)
    delete[]pAttr;

  return pRet;
}

char *
XMLNode::getAttributeValueByName(const char *pAName)
{
  if (!pAName)
    return NULL;

  char *p = NULL;
  for (int i = 0; i < m_nACount; i++) {
    if (!strcmp(pAName, m_pAList[i].pAName)) {
      p = m_pAList[i].pAValue;
      break;
    }
  }

  return p;
}

void /*XMLDom:: */
elementStart(void *pObj, const char *el, const char **attr)
{
  XMLDom *pDom = (XMLDom *) pObj;
  int nTagLen;
  char *pTag;

  if (!pDom->m_pCur) {
    pDom->m_pCur = (XMLNode *) pDom;
  } else {
    XMLNode *p = new XMLNode;
    pDom->m_pCur->AppendChild(p);
    pDom->m_pCur = p;
  }

  nTagLen = strlen(el);
  pTag = new char[nTagLen + 1];
  pDom->m_pCur->m_pNodeName = pTag;
  memcpy(pTag, el, nTagLen);
  pTag[nTagLen] = 0;

  int i;
  int nCount = 0;
  for (i = 0; attr[i]; i += 2)
    nCount++;                   /*Calculate how many elements. */
  if (nCount == 0)
    return;
  else
    pDom->m_pCur->m_nACount = nCount;

  pDom->m_pCur->m_pAList = new Attribute[nCount];
  for (i = 0; i < nCount; i++) {
    /* Attribute Name: attr[2*i], Value: attr[2*i+1]; */
    alloc_and_copy(&pDom->m_pCur->m_pAList[i].pAName, attr[2 * i]);
    alloc_and_copy(&pDom->m_pCur->m_pAList[i].pAValue, attr[2 * i + 1]);
  }
}

void /*XMLDom:: */
elementEnd(void *pObj, const char *el)
{
  NOWARN_UNUSED(el);
  /*ASSERT(strcmp(el, pCur->pNodeName) == 0); */
  XMLDom *pDom = (XMLDom *) pObj;
  pDom->m_pCur = pDom->m_pCur->m_pParentNode;
}

void /*XMLDom:: */
charHandler(void *pObj, const char *s, int len)
{
  XMLNode *pNode = ((XMLDom *) pObj)->m_pCur;
  int oldlen;
  char *p = pNode->m_pNodeValue;
  if (!p)
    oldlen = 0;
  else
    oldlen = strlen(p);

  char *pValue = new char[len + oldlen + 1];
  if (oldlen)
    memcpy(pValue, pNode->m_pNodeValue, oldlen);
  memcpy(pValue + oldlen, s, len);
  pValue[len + oldlen] = 0;

  if (pNode->m_pNodeValue)
    delete pNode->m_pNodeValue;
  pNode->m_pNodeValue = pValue;

  /*printf("Len: %d, Value: %s\n", len, pValue); */
}

int
XMLDom::LoadXML(const char *pXml)
{
  XML_Parser p = XML_ParserCreate(NULL);
  if (!p)                       /* no Memory. */
    return 1;


  XML_SetUserData(p, (void *) this);
  /* We can cast this. */
  XML_SetElementHandler(p,
                        /*(void (*)(void *, const char *, const char**)) */ elementStart,
                        /*(void (*)(void *, const char *)) */ elementEnd
    );

  XML_SetCharacterDataHandler(p,
                              /*(void (*)(void *, const char *, int)) */ charHandler
    );

  if (!XML_Parse(p, pXml, strlen(pXml), 0)) {
    /*return 2;     Parse Error: bad xml format. */
  }

  return 0;
}

int
XMLDom::LoadFile(const char *psFileName)
{
  int nSize;
  struct stat file_stat;
  char *pBuffer;
  FILE *pf;
  int n;

  // coverity[fs_check_call]
  if (stat(psFileName, &file_stat) != 0)
    return 3;                   /*File not existing or bad format. */
  nSize = file_stat.st_size;

  // coverity [alloc_fn][var_assign]
  pBuffer = new char[nSize + 1];
  if (pBuffer == NULL)          /*No memory */
    return 1;

  // coverity[toctou]
  pf = fopen(psFileName, "rb");
  if (pf == NULL)
    return 3;

  // coverity[noescape]
  n = fread(pBuffer, 1, nSize, pf);
  fclose(pf);
  if (n != nSize) {
    if (pBuffer)
      delete[]pBuffer;
    return 3;
  }

  pBuffer[nSize] = 0;
  n = LoadXML(pBuffer);
  delete[]pBuffer;
  return n;
}

XMLNode *
XMLNode::getNodeByPath(const char *pPath)
{
  if (!pPath)
    return NULL;

  char *p;
  // coverity[alloc_arg]
  alloc_and_copy(&p, pPath);
  // coverity[noescape]
  int len = strlen(p);
  int pos;
  for (pos = 0; pos < len; pos++) {
    if (p[pos] == '/')
      p[pos] = 0;
  }

  pos = 0;
  XMLNode *pCur = this;
  while (pos < len) {
    // coverity[noescape]
    pCur = pCur->getChildNode(p + pos);
    if (pCur == NULL) {
      if (p) {
        delete[]p;
      }
      return NULL;
    }
    // coverity[noescape]
    pos += strlen(p + pos) + 1;
  }

  delete[]p;
  return pCur;
}

char *
XMLNode::getNodeValue(const char *pPath)
{
  XMLNode *p = getNodeByPath(pPath);
  if (!p)
    return NULL;
  return p->getNodeValue();
}

void
XMLNode::WriteFile(FILE * pf)
{
  XMLNode *pChild = m_pFirstChild;
  if ((fwrite(gcstr_lb, 1, 1, pf) != 1) ||
      (fwrite(m_pNodeName, 1, strlen(m_pNodeName), pf) != strlen(m_pNodeName)))
    return;

  char *pAttr = getAttributeString();
  if (pAttr) {
    if (fwrite(pAttr, 1, strlen(pAttr), pf) != strlen(pAttr)) {
      delete[]pAttr;
      return;
    }
    delete[]pAttr;
  }

  if (fwrite(gcstr_rb, 1, 1, pf) != 1)
    return;
  if (m_pNodeValue)
    if (fwrite(m_pNodeValue, 1, strlen(m_pNodeValue), pf) != strlen(m_pNodeValue))
      return;

  while (pChild) {
    pChild->WriteFile(pf);
    pChild = pChild->m_pNext;
  }

  if ((fwrite(gcstr_lbend, 1, 2, pf) != 2) ||
      (fwrite(m_pNodeName, 1, strlen(m_pNodeName), pf) != strlen(m_pNodeName)) ||
      (fwrite(gcstr_rb, 1, 1, pf) != 1))
    return;

  return;
}

int
XMLDom::SaveToFile(const char *psFileName)
{
  FILE *pf;
  if (psFileName == NULL)
    return 3;                   /*Bad file Name */

  pf = fopen(psFileName, "wb");
  if (NULL == pf)
    return 3;                   /*Can't open file */
  WriteFile(pf);
  fclose(pf);

  return 0;
}

XMLDom::XMLDom()
{
  m_pCur = NULL;
}

XMLDom::~XMLDom()
{
  return;
}
