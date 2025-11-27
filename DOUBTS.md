# Doubts and Issues

## 2025-11-27 23:00

### GitLab Access Issue (confirmed)
- glab api projects/d33tah%2Fminimal-kernel-build returns 404 Project Not Found
- glab is configured for gitlab.profound.net with token
- User is 'claude' but project d33tah/minimal-kernel-build doesn't exist or no access
- All pushes going to GitHub only
- CI verification blocked - cannot run glab ci status

### Action taken
- All changes committed to GitHub
- Local make vm passes (Hello World! printed)
- Waiting for instructions on GitLab access

## 2025-11-27 22:55

### GitLab Access Issue
- Cannot push to GitLab remote (gitlab.profound.net)
- Error: "The project you were looking for could not be found or you don't have permission to view it"
- Only have access to GitHub origin
- Need to clarify if CI should run on GitHub or GitLab

### Goal Status
- Current kernel-only LOC: 210,330 (after mrproper)
- Goal: 200K LOC
- Still ~10.3K above target
- Session achieved ~500 LOC reduction through function stubbing
- Most obvious stubbing opportunities exhausted
- Further reduction requires more aggressive approaches (removing subsystems, header trimming)

### Questions
1. Is there a GitHub Actions CI or only GitLab CI?
2. Should I create an MR/PR on GitHub?
3. What permissions are needed for GitLab access?
