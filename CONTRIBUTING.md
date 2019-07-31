Contributing to Apache Traffic Server
=====================================

All contributions to the **ATS** project should be done via a Github Pull
Request (aka _PR_). If you are having an issue, but no code available yet, you
should file a Github Issue instead, which later will be linked to a new _PR_.

In general, all changes that are to be committed should have a Github
_PR_. There are some reasonable exceptions to this, but we should stick to the
Github workflow as agreed upon.


New Issues process replacing old Jira
-------------------------------------

1. If there is an issue/feature, an existing Jira Ticket, and no code, then
   create a Github _Issue_.  Copy the relevant information into the Github
   _Issue_ and request the Jira Ticket to be closed. Hopefully this use case
   won’t happen very often.

2. If there is an issue/feature and no code, then create a Github _Issue_.
   When there is code later, create a Github Pull Request and reference the
   Github _Issue_.

3. If there is an issue/feature and code, then create a Github Pull Request.
   If there is an existing Jira Ticket or Github _Issue_ refer to the Ticket
   or _Issue_ in the _PR_.  Creating a Github _Issue_ is not required for a
   Github Pull Request.


Making a good PR or Issue
-------------------------

Since Github _PRs_ and _Issues_ are now our primary way of tracking both code
changes and outstanding issues, it's important the we wall play nicely. Here
are a few simple rules to follow:

1. Always branch off the master branch, unless you are requesting a subsequent
   backport _PR_.

2. Make the subject line short (< 50 characters), but reasonably descriptive.

3. When filing a _PR_, without a previous _Issue_ describing the problem,
   please write something explaining the problem as part of the Description.

4. When filing an _Issue_, you should of course describe the problem, as well
   as any details such as platform, versions of software etc. used.

5. Make sure you set the appropriate _Milestone_, _Labels_, _Assignees_ and
   _Reviewers_.

6. If the _PR_ is a backport, or intended to be backported, please make sure to
   add the **Backport** label.

7. If the _PR_ is a Work-In-Progress, and not ready to commit, mark it with the
   **WIP** label.

8. Make sure you run **clang-format** before making the _PR_. This is easiest
   done with e.g. "make clang-format", which works on OSX and Linux.

9. When making backports, make sure you mark the _PR_ for the appropriate
   Github branch (e.g. **6.2.x**).

10. If you are making backports to an LTS branch, remember that the job of
   merging such a _PR_ is the duty of the release manager.


Merging a PR
------------

Only committers can merge a _PR_ (obviously), and any committer is allowed to
merge any _PR_ after review. A few requirements before merging must be met:

* Never, _ever_, merge a _PR_ which did not pass all the Jenkins build jobs!

* Make sure all attributes on the _PR_ and issue is satisfied, such as
  Milestone and Labels.

* Only merge a _PR_ that have at least one review approval, and no pending
  requested changes.

* Make sure the _PR_ is for the _master_ branch, only the RM should merge
  backport requested for her or his release branch.

* If there is also an open _Issue_ associated with the _PR_, make sure to
  close the _Issue_ as well.

Miscellaneous
-------------

Once your Github branch has been merged, it is safe to delete it from your
private repository.
